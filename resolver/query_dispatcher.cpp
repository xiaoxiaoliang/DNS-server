/**
 * @file query_dispatcher.cpp
 * @brief 查询调度器实现 —— 链式 DNS 解析引擎
 *
 * 线程安全: 调度器本身是无状态的（仅持有各解析器的指针），
 * 线程安全由各解析器内部自行保证（shared_mutex / mutex）。
 *
 * 事务 ID 处理:
 *   DNS 协议的 Transaction ID 由客户端在查询报文中设置，
 *   服务器响应时必须原样返回，以便客户端匹配请求和响应。
 *   因此从缓存中取出响应后，需要替换事务 ID 为当前查询的 ID。
 *
 * TTL 提取逻辑:
 *   从上游 DNS 响应报文的第一个回答记录中解析 TTL 字段，
 *   位置关系: HEADER(12B) → QUESTION(变长) → ANSWER_NAME → TYPE(2B) → CLASS(2B) → TTL(4B)
 */
#include "query_dispatcher.h"
#include "../dns/dns_parser.h"
#include "../dns/dns_builder.h"
#include "../dns/dns_header.h"
#include "../dns/dns_rr.h"
#include "../common/types.h"
#include "../common/logger.h"
#include "../monitor/stats.h"
#include "hosts_resolver.h"
#include "filter_resolver.h"
#include "cache_resolver.h"
#include "forward_resolver.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cstring>
#endif

QueryDispatcher::QueryDispatcher(HostsResolver* hosts, FilterResolver* filter,
                                 CacheResolver* cache, ForwardResolver* forward)
    : hosts_(hosts), filter_(filter), cache_(cache), forward_(forward) {}

std::vector<uint8_t> QueryDispatcher::process(const std::vector<uint8_t>& raw_query) {
    // -------------------------------------------------------------------
    // 步骤 0: 解析查询报文
    // -------------------------------------------------------------------
    auto parsed = DnsParser::parse_query(raw_query);
    if (!parsed.valid) {
        LOG_WARN("Failed to parse DNS query");
        return {};
    }

    const auto& domain = parsed.question.qname;     // 查询域名（含尾点）
    const auto& qtype = parsed.question.qtype;      // 查询类型
    const auto& header = parsed.header;             // 请求头部

    // 去除域名末尾的点 (DNS 域名编码约定: "example.com." → "example.com")
    std::string clean_domain = domain;
    if (!clean_domain.empty() && clean_domain.back() == '.')
        clean_domain.pop_back();

    // -------------------------------------------------------------------
    // 步骤 1: 检查 Hosts 文件 —— 最高优先级
    // -------------------------------------------------------------------
    auto host_result = hosts_->resolve(clean_domain);
    if (host_result.has_value()) {
        LOG_DEBUG("Hosts hit: " + clean_domain + " -> " + *host_result);
        auto resp = build_from_hosts(*host_result, qtype, domain);
        // 替换事务 ID 为当前查询的 ID
        if (resp.size() >= 2) {
            resp[0] = raw_query[0];
            resp[1] = raw_query[1];
        }
        return resp;
    }

    // -------------------------------------------------------------------
    // 步骤 2: 检查黑白名单 —— 拦截屏蔽域名
    // -------------------------------------------------------------------
    if (filter_->should_block(clean_domain)) {
        LOG_INFO("Blocked: " + clean_domain);
        Stats::instance().inc_blocked();
        return DnsBuilder::build_nxdomain(header, parsed.question);
    }

    // -------------------------------------------------------------------
    // 步骤 3: 检查本地缓存 —— 对 A(1)、AAAA(28)、PTR(12)、HTTPS(65) 记录缓存
    // -------------------------------------------------------------------
    if (qtype == 1 || qtype == 28 || qtype == 12 || qtype == 65) {
        auto cached = cache_->get(clean_domain, qtype);
        if (cached.has_value()) {
            LOG_DEBUG("Cache hit: " + clean_domain + " type=" + std::to_string(qtype));
            Stats::instance().inc_cache_hit();
            auto resp = *cached;
            // 替换事务 ID 以匹配当前查询
            if (resp.size() >= 2) {
                resp[0] = raw_query[0];
                resp[1] = raw_query[1];
            }
            return resp;
        }
        Stats::instance().inc_cache_miss();
    }

    // -------------------------------------------------------------------
    // 步骤 4: 转发到上游 DNS —— 最后的兜底策略
    // -------------------------------------------------------------------
    LOG_DEBUG("Forwarding: " + clean_domain + " type=" + std::to_string(qtype));
    Stats::instance().inc_forwarded();
    auto fwd_result = forward_->forward(raw_query);
    if (!fwd_result.has_value()) {
        // 所有上游服务器均失败 → 返回 SERVFAIL (RCODE=2)
        LOG_ERROR("Forward failed for: " + clean_domain);
        Stats::instance().inc_forward_fail();
        DnsHeader err_header = header;
        err_header.set_response(2);   // SERVFAIL
        return DnsBuilder::build_response(err_header, parsed.question, {}, 2);
    }

    auto response = *fwd_result;

    // -------------------------------------------------------------------
    // 步骤 5: 将转发结果缓存 —— 对 A/AAAA/PTR/HTTPS 记录
    // -------------------------------------------------------------------
    if (qtype == 1 || qtype == 28 || qtype == 12 || qtype == 65) {
        uint32_t ttl = 300;   // 默认 TTL = 5 分钟
        auto parsed_resp = DnsParser::parse_query(response);

        // 如果有回答记录，尝试从报文中提取 TTL 值
        if (parsed_resp.valid && parsed_resp.header.ancount > 0) {
            size_t offset = DnsHeader::SIZE;                   // 跳过头部

            // 跳过问题段: QNAME(变长) + QTYPE(2B) + QCLASS(2B)
            DnsParser::decode_domain(response, offset);
            offset += 4;  // QTYPE + QCLASS

            // 跳过第一个回答的 NAME 字段
            if (offset < response.size() && (response[offset] & 0xC0) == 0xC0)
                offset += 2;  // 压缩指针占 2 字节
            else
                DnsParser::decode_domain(response, offset); // 完整域名

            // 现在 offset 指向 TYPE → CLASS → TTL
            if (offset + 10 <= response.size()) {
                offset += 2;  // 跳过 TYPE
                offset += 2;  // 跳过 CLASS
                // 读取 4 字节 TTL (大端序)
                ttl = (uint32_t)response[offset] << 24 |
                      (uint32_t)response[offset+1] << 16 |
                      (uint32_t)response[offset+2] << 8 |
                      response[offset+3];

                // TTL 范围约束: 至少 60 秒，最多 24 小时
                if (ttl < 60) ttl = 60;
                if (ttl > 86400) ttl = 86400;
            }
        }
        cache_->put(clean_domain, qtype, response, ttl);
    }

    return response;
}

// ============================================================================
// 从 Hosts 解析结果构造 DNS 响应
//
// 根据查询类型决定返回 A 记录或 AAAA 记录:
//   - qtype == 1 (A): 只构造 IPv4 记录
//   - qtype == 28 (AAAA): 只构造 IPv6 记录
//   - qtype == 255 (ANY): 同时构造 IPv4 和 IPv6 记录
//
// IP 版本检测: 尝试 inet_pton 解析 IPv4 或 IPv6 格式以确定记录类型
// ============================================================================
std::vector<uint8_t> QueryDispatcher::build_from_hosts(const std::string& ip_str, uint16_t qtype,
                                                        const std::string& qname) {
    std::vector<DnsRR> answers;

    // 尝试构造 A 记录 (IPv4)
    if (qtype == 1 || qtype == 255) {
        struct in_addr addr;
        if (inet_pton(AF_INET, ip_str.c_str(), &addr) == 1) {   // 是有效的 IPv4 地址
            uint8_t ip[4];
            std::memcpy(ip, &addr, 4);
            answers.push_back(DnsRR::make_a(qname, 3600, ip));  // TTL=1小时
        }
    }

    // 尝试构造 AAAA 记录 (IPv6)
    if (qtype == 28 || qtype == 255) {
        struct in6_addr addr6;
        if (inet_pton(AF_INET6, ip_str.c_str(), &addr6) == 1) { // 是有效的 IPv6 地址
            uint8_t ip[16];
            std::memcpy(ip, &addr6, 16);
            answers.push_back(DnsRR::make_aaaa(qname, 3600, ip));
        }
    }

    // 构建响应头部
    DnsHeader header;
    header.id = 0;                  // 事务 ID 将在外部替换
    header.set_response(0);         // QR=1, RCODE=0 (NOERROR)
    header.set_rd(true);            // 期望递归
    header.set_ra(true);            // 支持递归
    header.qdcount = 1;
    header.ancount = (uint16_t)answers.size();

    DnsQuestion question;
    question.qname = qname;
    question.qtype = qtype;
    question.qclass = 1;            // IN (Internet)

    if (answers.empty()) {
        // IP 格式与查询类型不匹配 → 返回 NXDOMAIN
        return DnsBuilder::build_nxdomain(header, question);
    }
    return DnsBuilder::build_response(header, question, answers, 0);
}
