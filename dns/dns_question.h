/**
 * @file dns_question.h
 * @brief DNS 问题段 —— RFC 1035 第 4.1.2 节
 *
 * 问题段结构:
 *   QNAME  → 变长域名（以标签长度+标签内容编码，以 0x00 结尾）
 *   QTYPE  → 2 字节，查询的资源记录类型（例如 A=1, AAAA=28）
 *   QCLASS → 2 字节，查询类（通常为 IN=1）
 */
#pragma once
#include <string>
#include <cstdint>

struct DnsQuestion {
    std::string qname;   // 查询域名 —— 例如 "www.example.com" (不含末尾 0x00)
    uint16_t qtype;      // 查询类型 —— 1=A(IPv4), 28=AAAA(IPv6), 255=ANY(所有)
    uint16_t qclass;     // 查询类   —— 通常为 1 (IN, Internet)
};
