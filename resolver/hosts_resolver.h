/**
 * @file hosts_resolver.h
 * @brief Hosts 文件解析器 —— 从本地 hosts 文件中加载自定义域名映射
 *
 * 支持标准 hosts 文件格式:
 *   # 注释行
 *   192.168.1.100    internal.example.com
 *   10.0.0.1         local.dev
 *   ::1              ipv6.local
 *
 * 映射: domain → IP 字符串（支持 IPv4 和 IPv6）
 *
 * 解析优先级最高 —— 本地 hosts 的解析结果优先于过滤器和上游 DNS
 */
#pragma once
#include <string>
#include <unordered_map>
#include "../common/optional.h"

class HostsResolver {
public:
    HostsResolver();
    ~HostsResolver() = default;

    /** 加载 hosts 文件，返回是否成功（文件不存在则失败） */
    bool load(const std::string& file_path);

    /** 查询域名对应的 IP 地址，未匹配则返回 nullopt */
    optional<std::string> resolve(const std::string& domain) const;

private:
    static std::string trim(const std::string& s);

    std::unordered_map<std::string, std::string> hosts_;   // domain → IP 映射表
};
