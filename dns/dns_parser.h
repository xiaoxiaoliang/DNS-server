/**
 * @file dns_parser.h
 * @brief DNS 报文解析器 —— 从原始字节流中提取头部和问题段
 *
 * 实现 RFC 1035 第 4.1.4 节的域名压缩指针解析算法。
 *
 * DNS 域名编码方式:
 *   - 标准编码: 每个标签先写 1 字节长度，再写标签内容，最后以 0x00 结束
 *   - 压缩指针: 若某个字节高 2 位为 11 (0xC0)，则该字节 + 下一字节组成
 *               14 位偏移量，指向报文其他位置的域名片段（复用已解析的部分）
 *
 * 例如报文中有两个域名 "www.example.com" 和 "ftp.example.com"，
 * 第二个域名的 "example.com" 部分可能用压缩指针指回第一个域名的对应位置，
 * 以节省报文空间。
 */
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "dns_header.h"
#include "dns_question.h"
#include "dns_rr.h"

class DnsParser {
public:
    /** 解析返回值 —— 包含解析出的头部、第一个问题段和有效性标志 */
    struct Result {
        DnsHeader   header;      // 解析后的 DNS 头部
        DnsQuestion question;    // 第一个问题段
        bool        valid = false;  // 解析是否成功
    };

    // ========================================================================
    // 快速解析 —— 仅提取头部 + 第一个问题段（解析器链的快速路径）
    // 若 QDCOUNT != 1 或不是查询报文则 valid = false
    // ========================================================================
    static Result parse_query(const std::vector<uint8_t>& packet) {
        Result r;

        // 报文至少要有 12 字节（DNS 头部大小）
        if (packet.size() < DnsHeader::SIZE) return r;

        // 解析头部
        r.header = DnsHeader::decode(packet.data());

        // 只处理标准查询（QR=0 且只有 1 个问题）
        if (!r.header.is_query() || r.header.qdcount != 1) return r;

        // 解析第一个问题段
        size_t offset = DnsHeader::SIZE;                     // 从头部之后开始
        r.question.qname = decode_domain(packet, offset);    // 解码域名（支持压缩指针）

        // 确保有足够字节存放 QTYPE(2字节) + QCLASS(2字节)
        if (offset + 4 > packet.size()) return r;

        r.question.qtype  = (uint16_t)packet[offset] << 8 | packet[offset+1];
        r.question.qclass = (uint16_t)packet[offset+2] << 8 | packet[offset+3];

        r.valid = true;
        return r;
    }

    // ========================================================================
    // 域名解码 —— 递归处理标签和压缩指针
    //
    // 压缩指针格式: 若某个字节高 2 位 = 0b11，则该字节低 6 位 + 下 1 字节
    // 组成 14 位偏移量（最大 16383），表示跳转到报文中该偏移处继续读取。
    //
    // 例如 "www.example.com" 在报文偏移 0x0C 处，
    // 后续域名可能用指针 {0xC0, 0x0C} 表示引用同样的域名。
    //
    // @param packet  完整的 DNS 报文
    // @param offset  当前读取位置，解析完成后指向域名之后的下一个字节
    // @return        解码出的点分域名（末尾无点），例如 "www.example.com"
    // ========================================================================
    static std::string decode_domain(const std::vector<uint8_t>& packet, size_t& offset) {
        std::string result;
        size_t jumped_offset = 0;    // 第一次跳转前的原始位置（用于恢复）
        bool jumped = false;         // 是否曾通过压缩指针跳转
        size_t max_hops = 10;        // 最大跳转次数限制 —— 防止恶意报文循环跳转

        while (max_hops-- > 0) {
            if (offset >= packet.size()) break;

            uint8_t len = packet[offset];

            // ---------------------------------------------------------------
            // 检测到压缩指针: 高 2 位 == 11 (0xC0)
            // 格式: 11000000 00000000 → 低 14 位为偏移量
            // ---------------------------------------------------------------
            if ((len & 0xC0) == 0xC0) {
                if (offset + 2 > packet.size()) break;
                size_t ptr = ((len & 0x3F) << 8) | packet[offset + 1];  // 提取14位偏移

                if (!jumped) jumped_offset = offset + 2;  // 记录首次跳转位置
                offset = ptr;                              // 跳转到指针指向的位置
                jumped = true;
                continue;
            }

            // ---------------------------------------------------------------
            // 遇到 0x00 → 域名结束
            // ---------------------------------------------------------------
            if (len == 0) {
                if (!jumped) offset++;           // 未跳转过，offset 前进 1 字节
                else offset = jumped_offset;     // 跳转过，恢复到跳转前的后续位置
                if (!result.empty() && result.back() == '.')
                    result.pop_back();           // 去掉末尾多余的点
                break;
            }

            // ---------------------------------------------------------------
            // 普通标签: 1字节长度 + N字节标签内容
            // 例如 0x03 'w' 'w' 'w' → 追加 "www." 到结果
            // ---------------------------------------------------------------
            offset++;
            if (offset + len > packet.size()) break;   // 越界保护
            result.append((const char*)&packet[offset], len);   // 追加标签内容
            result += '.';                            // 追加分隔点
            offset += len;
        }
        return result;
    }
};
