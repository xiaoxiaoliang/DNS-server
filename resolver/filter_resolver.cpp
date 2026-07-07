/**
 * @file filter_resolver.cpp
 * @brief 域名过滤器的 Trie 树实现
 *
 * Trie 树的匹配逻辑:
 *   以域名 "ads.doubleclick.net" 为例:
 *     1. 反转标签: {"net", "doubleclick", "ads"}
 *     2. 先在黑名单树中从根开始精确匹配 3 层标签
 *     3. 若无精确匹配，则 skip 1 个标签再从第 2 层开始匹配
 *     4. 继续 skip，直到只剩余 1 个标签或命中
 *
 *   通配符 "*.doubleclick.net" 只插入 {"net", "doubleclick"} 两层，
 *   匹配时 skip 掉 "ads" 标签后恰好命中这两个标签，实现通配效果。
 */
#include "filter_resolver.h"
#include <fstream>
#include <algorithm>
#include "../common/logger.h"

// ============================================================================
// TrieNode 析构 —— 递归销毁所有子节点
// ============================================================================
TrieNode::~TrieNode() {
    for (auto& kv : children) delete kv.second;
}

FilterResolver::FilterResolver() = default;
FilterResolver::~FilterResolver() = default;

// ============================================================================
// 加载黑名单
// ============================================================================
size_t FilterResolver::load_blocklist(const std::string& file_path) {
    std::ifstream f(file_path);
    if (!f.is_open()) return 0;

    std::vector<std::string> domains;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (!line.empty() && line[0] != '#') domains.push_back(line);  // 跳过空行和注释
    }

    for (const auto& d : domains) insert(d, &block_root_, true);
    block_count_ = domains.size();
    LOG_INFO("Loaded " + std::to_string(block_count_) + " blocklist entries from " + file_path);
    return domains.size();
}

// ============================================================================
// 加载白名单
// ============================================================================
size_t FilterResolver::load_whitelist(const std::string& file_path) {
    std::ifstream f(file_path);
    if (!f.is_open()) return 0;

    std::vector<std::string> domains;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (!line.empty() && line[0] != '#') domains.push_back(line);
    }

    for (const auto& d : domains) insert(d, &white_root_, false);
    white_count_ = domains.size();
    LOG_INFO("Loaded " + std::to_string(white_count_) + " whitelist entries from " + file_path);
    return domains.size();
}

// ============================================================================
// 查询接口 —— 使用 shared_lock 允许多线程并发查询
// ============================================================================
bool FilterResolver::should_block(const std::string& domain) const {
    // 有白名单时：只允许白名单域名通过，黑名单始终拦截
    if (white_count_ > 0) {
        if (match(domain, &block_root_, true)) return true;   // 在黑名单中 → 拦截（即使也在白名单）
        if (match(domain, &white_root_, false)) return false; // 仅在白名单中 → 放行
        return true;                                           // 都不在 → 拦截
    }
    // 无白名单时：拦截黑名单域名，其余放行
    return match(domain, &block_root_, true);
}

// ============================================================================
// 字符串截断
// ============================================================================
std::string FilterResolver::trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ============================================================================
// 插入域名到 Trie 树
// ============================================================================
void FilterResolver::insert(const std::string& domain, TrieNode* root, bool is_block) {
    // 检测通配符: *.example.com → 去掉 "*." 前缀，只保留 "example.com"
    bool wildcard = (domain.size() > 1 && domain[0] == '*' && domain[1] == '.');
    std::string d = wildcard ? domain.substr(2) : domain;

    std::vector<std::string> labels = split_reverse(d);  // 反转标签

    TrieNode* node = root;
    for (const auto& label : labels) {
        if (node->children.find(label) == node->children.end()) {
            node->children[label] = new TrieNode();       // 不存在则创建新结点
        }
        node = node->children[label];
    }
    // 设置末端结点的标志位
    if (is_block) node->is_blocked = true;
    else node->is_whitelisted = true;
}

// ============================================================================
// 域名匹配 —— 尝试从完整域名的不同层级开始匹配
// ============================================================================
bool FilterResolver::match(const std::string& domain, const TrieNode* root, bool check_blocked) const {
    std::vector<std::string> labels = split_reverse(domain);  // 反转标签

    // 步骤 1: 尝试精确匹配（从第 0 层开始，匹配所有标签）
    if (match_exact(labels, 0, root, check_blocked)) return true;

    // 步骤 2: 尝试通配匹配（skip 掉前面的子域名标签）
    for (size_t skip = 1; skip < labels.size(); ++skip) {
        if (match_exact(labels, skip, root, check_blocked)) return true;
    }
    return false;
}

bool FilterResolver::match_exact(const std::vector<std::string>& labels,
                                  size_t start, const TrieNode* node, bool check_blocked) const {
    for (size_t i = start; i < labels.size(); ++i) {
        auto it = node->children.find(labels[i]);
        if (it == node->children.end()) return false;  // 标签不匹配，直接失败
        node = it->second;                              // 进入下一层
    }
    // 根据参数检查黑名单或白名单标志位
    return check_blocked ? node->is_blocked : node->is_whitelisted;
}

// ============================================================================
// 域名标签反转 —— 从根域名开始便于 Trie 树匹配
// 例如 "www.example.com" → {"com", "example", "www"}
// ============================================================================
std::vector<std::string> FilterResolver::split_reverse(const std::string& domain) {
    std::vector<std::string> labels;
    size_t start = 0;
    while (start < domain.size()) {
        auto dot = domain.find('.', start);
        if (dot == std::string::npos) {
            labels.push_back(domain.substr(start));        // 最后一个标签
            break;
        }
        labels.push_back(domain.substr(start, dot - start)); // 当前标签
        start = dot + 1;
    }
    std::reverse(labels.begin(), labels.end());            // 反转：根域名在前
    return labels;
}
