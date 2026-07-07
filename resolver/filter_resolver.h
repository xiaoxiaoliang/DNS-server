/**
 * @file filter_resolver.h
 * @brief DNS 域名过滤器 —— 基于 Trie 树的黑白名单匹配
 *
 * 从文本文件加载黑白名单（每行一个域名），支持:
 *   - 精确匹配: "doubleclick.net"
 *   - 通配符匹配: "*.doubleclick.net" 匹配任意子域名
 *   - 过滤策略: 有白名单时仅允许白名单域名通过（黑名单仍拦截），
 *     无白名单时仅拦截黑名单域名
 *
 * 黑白名单文件格式:
 *   # 这是一行注释
 *   doubleclick.net          # 精确域名
 *   *.googleadservices.com   # 通配符域名 (匹配所有子域名)
 *   adservice.google.com
 *
 * Trie 树的设计:
 *   - 域名被反序切割为标签 (com → example → www)
 *   - 从根节点向下遍历，匹配到叶子节点即表示命中
 *   - 通配符 *.domain.com 去掉 * 后按 domain.com 插入，
 *     匹配时通过 skip 机制实现通配效果
 */
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstddef>

/** Trie 树结点 —— 每个结点代表域名中的一个标签层级 */
struct TrieNode {
    std::unordered_map<std::string, TrieNode*> children;  // 子节点: 标签名 → 子结点指针
    bool is_blocked = false;      // 该结点对应的域名是否在黑名单中
    bool is_whitelisted = false;  // 该结点对应的域名是否在白名单中
    ~TrieNode();                  // 析构时递归释放所有子节点
};

class FilterResolver {
public:
    FilterResolver();
    ~FilterResolver();

    /**
     * 从文件加载黑名单 —— 每行一个域名，以 # 开头的行为注释
     * @return 成功加载的域名数量
     */
    size_t load_blocklist(const std::string& file_path);

    /**
     * 从文件加载白名单 —— 格式与黑名单相同
     * @return 成功加载的域名数量
     */
    size_t load_whitelist(const std::string& file_path);

    /**
     * 综合判断:
     *   - 有白名单: 黑名单优先拦截，白名单中的放行，其余拦截
     *   - 无白名单: 黑名单中的拦截，其余放行
     * @return true 表示应该拦截该域名
     */
    bool should_block(const std::string& domain) const;

private:
    /** 去除字符串首尾空白字符 */
    static std::string trim(const std::string& s);

    /**
     * 向 Trie 树插入一个域名
     * @param domain  域名（支持 "*.example.com" 通配符格式）
     * @param root    目标 Trie 树的根结点
     * @param is_block  true=插入黑名单, false=插入白名单
     */
    void insert(const std::string& domain, TrieNode* root, bool is_block);

    /** 在指定 Trie 树中匹配域名 —— 支持通配符 skip 匹配
     *  @param check_blocked  true=检查 is_blocked 标志, false=检查 is_whitelisted 标志 */
    bool match(const std::string& domain, const TrieNode* root, bool check_blocked) const;

    /**
     * 精确标签序列匹配 —— 从 start 位置开始逐层匹配
     * 匹配到叶子节点时根据 check_blocked 检查对应的标志位
     */
    bool match_exact(const std::vector<std::string>& labels, size_t start,
                     const TrieNode* node, bool check_blocked) const;

    /**
     * 将域名按 "." 分割并反转标签顺序
     * 例如 "www.example.com" → {"com", "example", "www"}
     * 反转后从根域名开始匹配，自然兼容通配符 skip 策略
     */
    static std::vector<std::string> split_reverse(const std::string& domain);

    TrieNode block_root_;          // 黑名单 Trie 树根节点
    TrieNode white_root_;          // 白名单 Trie 树根节点
    size_t block_count_ = 0;       // 黑名单域名计数
    size_t white_count_ = 0;       // 白名单域名计数
};
