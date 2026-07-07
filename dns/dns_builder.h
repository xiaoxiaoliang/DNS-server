/**
 * @file dns_builder.h
 * @brief DNS 响应报文构造器
 *
 * 将内存中的 DNS 数据（头部、问题段、资源记录列表）序列化为
 * 符合 RFC 1035 的完整二进制报文，支持:
 *   - 标准成功响应（带回答记录）
 *   - NXDOMAIN 响应（域名不存在）
 *   - 域名压缩 —— 问题段域名出现一次后，回答段用指针复用
 *
 * 域名压缩原理:
 *   DNS 报文中完全相同的域名后缀可用 2 字节指针代替完整编码。
 *   Builder 在构造时自动记录每个域名首次出现的位置，
 *   后续相同的域名直接用 {0xC0, offset} 的形式引用。
 */
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include "dns_header.h"
#include "dns_question.h"
#include "dns_rr.h"
#include <iostream>

class DnsBuilder {
public:
    /**
     * 构造标准 DNS 响应报文
     *
     * @param req_header  请求头部（用于提取事务 ID 等字段）
     * @param question    请求中的问题段（用于回显）
     * @param answers     回答段的资源记录列表
     * @param rcode       响应码: 0=NOERROR, 3=NXDOMAIN, 2=SERVFAIL
     * @return            完整的 DNS 响应报文（二进制）
     */
    static std::vector<uint8_t> build_response(const DnsHeader& req_header,
                                                const DnsQuestion& question,
                                                const std::vector<DnsRR>& answers,
                                                uint8_t rcode) {
        DnsHeader header = req_header;
        header.set_response(rcode);                  // 设置 QR=1, RCODE=指定值
        header.set_ra(false);                        // 递归不可用（本服务器不递归）
        header.set_aa(false);                        // 非权威回答
        header.qdcount = 1;                          // 回显 1 个问题
        header.ancount = (uint16_t)answers.size();   // 回答记录数
        header.nscount = 0;                          // 不提供授权段
        header.arcount = 0;                          // 不提供附加段

        return build_raw(header, question, answers);
    }

    /**
     * 构造 NXDOMAIN 响应 —— 告知客户端查询的域名不存在
     * RCODE=3, ANCOUNT=0 (无回答记录)
     */
    static std::vector<uint8_t> build_nxdomain(const DnsHeader& req_header,
                                                const DnsQuestion& question) {
        DnsHeader header = req_header;
        header.set_response(3);    // RCODE=3 (NXDOMAIN)
        header.set_ra(false);
        header.set_aa(false);
        header.qdcount = 1;        // 回显 1 个问题
        header.ancount = 0;        // 无回答
        header.nscount = 0;
        header.arcount = 0;

        return build_raw(header, question, {});
    }

private:
    /**
     * 底层序列化 —— 将头部、问题段、回答段拼接为完整报文
     *
     * 编码顺序:
     *   [12字节头部] → [问题段(QNAME+QTYPE+QCLASS)] → [回答段(各RR的NAME+TYPE+CLASS+TTL+RDATA)]
     *
     * 回答段的域名如与问题段域名相同，使用 2 字节压缩指针（引用头部之后的位置）
     */
    static std::vector<uint8_t> build_raw(const DnsHeader& header,
                                           const DnsQuestion& question,
                                           const std::vector<DnsRR>& answers) {
        std::vector<uint8_t> buf;
        buf.resize(DnsHeader::SIZE);                  // 预留 12 字节头部空间
        header.encode(buf.data());                    // 写入头部

        // ===================================================================
        // 编码问题段: QNAME (变长域名) + QTYPE(2B) + QCLASS(2B)
        // ===================================================================
        encode_domain(question.qname, buf);
        buf.push_back(question.qtype >> 8);           // QTYPE 高字节
        buf.push_back(question.qtype & 0xFF);         // QTYPE 低字节
        buf.push_back(question.qclass >> 8);          // QCLASS 高字节
        buf.push_back(question.qclass & 0xFF);        // QCLASS 低字节

        // ===================================================================
        // 构建压缩字典 —— 记录问题段域名出现的位置
        // key=完整域名, value=在报文中的偏移位置
        // 回答段中若域名与之相同，则用压缩指针 {0xC0, offset} 代替
        // ===================================================================
        std::unordered_map<std::string, size_t> dict;
        dict[question.qname] = DnsHeader::SIZE;       // 问题段域名位于头部之后

        // 编码所有回答记录
        for (const auto& ans : answers) {
            encode_rr(ans, buf, dict);
        }

        return buf;
    }

    // -----------------------------------------------------------------------
    // 域名编码 —— 将点分域名编码为 DNS 标签序列
    // 例如 "www.example.com" →
    //   [03]'w''w''w' [07]'e''x''a''m''p''l''e' [03]'c''o''m' [00]
    // -----------------------------------------------------------------------
    static void encode_domain(const std::string& domain, std::vector<uint8_t>& out) {
        size_t start = 0;
        while (start < domain.size()) {
            auto dot = domain.find('.', start);
            if (dot == std::string::npos) dot = domain.size();
            size_t len = dot - start;
            if (len > 63) {
                std::cerr << "[WARN] Domain label too long (" << len
                          << " bytes), truncating: " << domain << std::endl;
                break;                              // 标签长度上限（RFC 1035）
            }
            out.push_back((uint8_t)len);              // 写入标签长度
            out.insert(out.end(), domain.begin() + start, domain.begin() + dot); // 标签内容
            start = dot + 1;
        }
        if (out.empty() || out.back() != 0) out.push_back(0);  // 以 0x00 结尾
    }

    // -----------------------------------------------------------------------
    // 单个资源记录编码
    //
    // 若 rr.name 已出现在压缩字典中，则使用 2 字节压缩指针，
    // 否则完整编码域名并登记到字典。
    //
    // 编码格式: NAME | TYPE(2B) | CLASS(2B) | TTL(4B) | RDLEN(2B) | RDATA(NB)
    // -----------------------------------------------------------------------
    static void encode_rr(const DnsRR& rr, std::vector<uint8_t>& out,
                          std::unordered_map<std::string, size_t>& dict) {
        // 检查是否可以压缩
        auto it = dict.find(rr.name);
        if (it != dict.end()) {
            // 使用压缩指针: 0xC0 | (offset高6位), (offset低8位)
            // DNS 压缩指针只有 14 位，最大偏移 16383
            size_t ptr = it->second;
            if (ptr > 0x3FFF) {
                // 偏移超出压缩指针范围，退化为完整域名编码
                encode_domain(rr.name, out);
            } else {
                out.push_back(0xC0 | (uint8_t)(ptr >> 8));
                out.push_back((uint8_t)(ptr & 0xFF));
            }
        } else {
            dict[rr.name] = out.size();   // 登记域名位置到字典
            encode_domain(rr.name, out);  // 完整编码
        }

        // TYPE + CLASS（各 2 字节，大端序）
        out.push_back(rr.type >> 8);       out.push_back(rr.type & 0xFF);
        out.push_back(rr.rclass >> 8);     out.push_back(rr.rclass & 0xFF);

        // TTL（4 字节，大端序）
        out.push_back(rr.ttl >> 24);       out.push_back((rr.ttl >> 16) & 0xFF);
        out.push_back((rr.ttl >> 8) & 0xFF); out.push_back(rr.ttl & 0xFF);

        // RDLEN + RDATA
        out.push_back(static_cast<uint8_t>(rr.rdata.size() >> 8));   // RDATA 长度高字节
        out.push_back(static_cast<uint8_t>(rr.rdata.size() & 0xFF)); // RDATA 长度低字节
        out.insert(out.end(), rr.rdata.begin(), rr.rdata.end()); // RDATA 数据体
    }
};
