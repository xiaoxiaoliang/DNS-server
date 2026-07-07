/**
 * @file dns_header.h
 * @brief DNS 报文头部 —— RFC 1035 第 4.1.1 节
 *
 * DNS 报文头部固定 12 字节，结构如下:
 *   ┌────────────────────────────────┐
 *   │         Transaction ID (16)    │  ← 请求和响应配对标识
 *   ├────────────────────────────────┤
 *   │ QR│ OPCODE │AA│TC│RD│RA│Z│RCODE│  ← 标志位 (16)
 *   ├────────────────────────────────┤
 *   │         QDCOUNT (16)           │  ← 问题段记录数
 *   ├────────────────────────────────┤
 *   │         ANCOUNT (16)           │  ← 回答段记录数
 *   ├────────────────────────────────┤
 *   │         NSCOUNT (16)           │  ← 授权段记录数
 *   ├────────────────────────────────┤
 *   │         ARCOUNT (16)           │  ← 附加段记录数
 *   └────────────────────────────────┘
 *
 * FLAGS 字段位布局:
 *   bit 15: QR    (0=查询, 1=响应)
 *   bit 14-11: OPCODE (0=标准查询, 其他值见 RFC)
 *   bit 10: AA    (1=权威回答)
 *   bit 9: TC     (1=截断)
 *   bit 8: RD     (1=期望递归查询)
 *   bit 7: RA     (1=服务器支持递归)
 *   bit 6-4: Z     (保留，必须为 0)
 *   bit 3-0: RCODE (响应码: 0=NOERROR, 3=NXDOMAIN 等)
 */
#pragma once
#include <cstdint>

struct DnsHeader {
    uint16_t id;        // 事务 ID —— 客户端生成，服务端原样返回以配对请求和响应

    uint16_t flags;     // 标志位 —— 包含 QR/OPCODE/AA/TC/RD/RA/Z/RCODE

    uint16_t qdcount;   // 问题计数 —— 问题段中的查询数（标准查询始终为 1）
    uint16_t ancount;   // 回答计数 —— 回答段中的资源记录数
    uint16_t nscount;   // 授权计数 —— 授权段中的资源记录数
    uint16_t arcount;   // 附加计数 —— 附加段中的资源记录数

    /** 头部固定字节数: 6 个 u16 字段 × 2 字节 = 12 字节 */
    static constexpr size_t SIZE = 12;

    // ========================================================================
    // 编解码 —— 大端序（网络字节序）
    // ========================================================================

    /** 从原始字节数组解析 DNS 头部 */
    static DnsHeader decode(const uint8_t* data) {
        DnsHeader h;
        h.id      = (uint16_t)data[0] << 8 | data[1];
        h.flags   = (uint16_t)data[2] << 8 | data[3];
        h.qdcount = (uint16_t)data[4] << 8 | data[5];
        h.ancount = (uint16_t)data[6] << 8 | data[7];
        h.nscount = (uint16_t)data[8] << 8 | data[9];
        h.arcount = (uint16_t)data[10] << 8 | data[11];
        return h;
    }

    /** 将 DNS 头部编码为 12 字节原始数据 */
    void encode(uint8_t* data) const {
        data[0] = id >> 8;      data[1] = id & 0xFF;
        data[2] = flags >> 8;   data[3] = flags & 0xFF;
        data[4] = qdcount >> 8; data[5] = qdcount & 0xFF;
        data[6] = ancount >> 8; data[7] = ancount & 0xFF;
        data[8] = nscount >> 8; data[9] = nscount & 0xFF;
        data[10] = arcount >> 8; data[11] = arcount & 0xFF;
    }

    // ========================================================================
    // 标志位查询
    // ========================================================================

    /** 检查是否为 DNS 查询报文（QR=0 表示查询，QR=1 表示响应） */
    bool is_query() const { return (flags & 0x8000) == 0; }

    /** 提取操作码 (OPCODE) —— 标准查询为 0 */
    uint8_t opcode() const { return (flags >> 11) & 0x0F; }

    /** 提取响应码 (RCODE) —— 低 4 位: 0=成功 3=域名不存在 */
    uint8_t rcode()  const { return flags & 0x0F; }

    /** 期望递归标志 (RD) —— 客户端是否请求服务器进行递归查询 */
    bool rd() const { return (flags & 0x0100) != 0; }

    // ========================================================================
    // 标志位设置
    // ========================================================================

    /** 设为响应报文 —— 设置 QR=1 并填入响应码 */
    void set_response(uint8_t rcode) {
        flags = (flags & ~0x000F) | (rcode & 0x0F); // 清除旧 RCODE，写入新值
        flags |= 0x8000;                              // 设置 QR=1 (响应)
    }

    void set_aa(bool aa)  { flags = aa ? (flags | 0x0400) : (flags & ~0x0400); }  // 权威回答标志
    void set_ra(bool ra)  { flags = ra ? (flags | 0x0080) : (flags & ~0x0080); }  // 递归可用标志
    void set_rd(bool rd)  { flags = rd ? (flags | 0x0100) : (flags & ~0x0100); }  // 期望递归标志
};
