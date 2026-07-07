/**
 * @file query_dispatcher.h
 * @brief 查询调度器 —— 串联 DNS 解析的所有阶段
 *
 * 解析器链设计模式: 将解析过程分解为独立的阶段，按优先级依次尝试。
 * 每个阶段都有明确的职责和失败处理策略。
 *
 * 解析优先级 (从高到低):
 *   1. Hosts 解析        —— 本地 hosts 文件定义的域名映射
 *   2. 黑白名单过滤      —— 拦截广告域名等（白名单优先放行）
 *   3. 本地缓存 (LRU)    —— 之前解析过的结果，命中直接返回
 *   4. 上游 DNS 转发     —— 向上游服务器查询，结果写入缓存
 *
 * 关键设计:
 *   - 每个阶段失败后自动进入下一阶段
 *   - 缓存仅对 A(1) 和 AAAA(28) 记录生效
 *   - 转发成功后自动缓存结果到 LRU
 *   - 从上游响应中提取 TTL 值设置缓存过期时间
 */
#pragma once
#include <vector>
#include <string>
#include <cstdint>

class HostsResolver;
class FilterResolver;
class CacheResolver;
class ForwardResolver;

class QueryDispatcher {
public:
    QueryDispatcher(HostsResolver* hosts, FilterResolver* filter,
                    CacheResolver* cache, ForwardResolver* forward);

    /**
     * 处理一次 DNS 查询 —— 返回完整的 DNS 响应报文
     * @param raw_query  来自客户端的原始 DNS 查询报文
     * @return           构造好的 DNS 响应报文 (空 vector 表示无法处理)
     */
    std::vector<uint8_t> process(const std::vector<uint8_t>& raw_query);

private:
    /**
     * 从 Hosts 解析结果构造 DNS 响应报文
     * @param ip_str   Hosts 文件中的 IP 地址字符串
     * @param qtype    查询类型 (A=1, AAAA=28, ANY=255)
     * @param qname    实际查询的域名（用于构造正确的响应）
     */
    std::vector<uint8_t> build_from_hosts(const std::string& ip_str, uint16_t qtype,
                                           const std::string& qname);

    HostsResolver* hosts_;     // Hosts 解析器
    FilterResolver* filter_;   // 域名过滤器 (黑白名单)
    CacheResolver* cache_;     // DNS 缓存
    ForwardResolver* forward_; // 上游 DNS 转发器
};
