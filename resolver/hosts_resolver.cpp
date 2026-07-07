/**
 * @file hosts_resolver.cpp
 * @brief Hosts 文件加载和查询实现
 *
 * hosts 文件解析规则:
 *   - 每行格式: IP地址  域名  [可选别名]
 *   - IP 和域名之间用空格或制表符分隔
 *   - 以 # 开头的行为注释
 *   - 空行被忽略
 *   - 域名末尾的 "." 被自动移除
 */
#include "hosts_resolver.h"
#include <fstream>
#include "../common/logger.h"

HostsResolver::HostsResolver() = default;

bool HostsResolver::load(const std::string& file_path) {
    std::ifstream f(file_path);
    if (!f.is_open()) return false;          // 文件不存在则加载失败

    // 先在临时 map 中构建新数据，避免长时间持有锁
    std::unordered_map<std::string, std::string> tmp;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);

        // 跳过空行和注释行
        if (line.empty() || line[0] == '#') continue;

        // 找到 IP 和域名之间的分隔符（第一个空格或制表符）
        auto space = line.find_first_of(" \t");
        if (space == std::string::npos) continue;

        std::string ip = line.substr(0, space);              // IP 地址
        std::string domain = trim(line.substr(space + 1));   // 域名（去除多余空白）

        // 域名处理: 去尾点、忽略空白域名
        if (!domain.empty() && domain.back() == '.') domain.pop_back();
        if (!domain.empty()) tmp[domain] = ip;
    }

    // 原子替换: 用新数据覆盖旧数据
    hosts_ = std::move(tmp);
    LOG_INFO("Loaded " + std::to_string(hosts_.size()) + " hosts entries from " + file_path);
    return true;
}

optional<std::string> HostsResolver::resolve(const std::string& domain) const {
    auto it = hosts_.find(domain);
    if (it != hosts_.end()) return it->second;
    return nullopt;
}

std::string HostsResolver::trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}
