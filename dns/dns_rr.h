/**
 * @file dns_rr.h
 * @brief DNS 资源记录 —— RFC 1035 第 4.1.3 节
 *
 * 资源记录（Resource Record）结构:
 *   NAME   → 变长域名（或压缩指针）
 *   TYPE   → 2 字节，记录类型 (A=1, AAAA=28, CNAME=5 等)
 *   CLASS  → 2 字节，记录类 (通常 IN=1)
 *   TTL    → 4 字节，缓存生存时间 (秒)
 *   RDLEN  → 2 字节，RDATA 数据长度
 *   RDATA  → RDLEN 字节，具体数据 (A 记录为 4 字节 IPv4 地址)
 *
 * 提供便捷工厂方法用于快速构造 A/AAAA/CNAME 记录。
 */
#pragma once
#include <string>
#include <cstdint>
#include <vector>

struct DnsRR {
    std::string name;                // 资源记录对应的域名
    uint16_t    type;                // 记录类型 (1=A, 5=CNAME, 28=AAAA)
    uint16_t    rclass;              // 记录类 (1=IN)
    uint32_t    ttl;                 // 生存时间 (秒)
    std::vector<uint8_t> rdata;      // 记录数据体的原始字节

    // -----------------------------------------------------------------------
    // 便捷工厂方法 —— 按 DNS 协议要求构造资源记录
    // -----------------------------------------------------------------------

    /**
     * 构造 A 记录 —— 将域名映射为 IPv4 地址
     * @param name  域名 (如 "www.example.com")
     * @param ttl   缓存时间 (秒)，例如 3600 = 1小时
     * @param ip    4 字节 IPv4 地址，大端序 (例如 {192,168,1,1})
     */
    static DnsRR make_a(const std::string& name, uint32_t ttl, const uint8_t ip[4]) {
        DnsRR rr;
        rr.name = name;
        rr.type = 1;                       // TYPE=A
        rr.rclass = 1;                     // CLASS=IN
        rr.ttl = ttl;
        rr.rdata.assign(ip, ip + 4);       // RDATA = 4字节IPv4地址
        return rr;
    }

    /**
     * 构造 AAAA 记录 —— 将域名映射为 IPv6 地址
     * @param ip 16 字节 IPv6 地址
     */
    static DnsRR make_aaaa(const std::string& name, uint32_t ttl, const uint8_t ip[16]) {
        DnsRR rr;
        rr.name = name;
        rr.type = 28;                      // TYPE=AAAA
        rr.rclass = 1;
        rr.ttl = ttl;
        rr.rdata.assign(ip, ip + 16);      // RDATA = 16字节IPv6地址
        return rr;
    }
};
