/**
 * @file buffer.h
 * @brief 字节缓冲区工具 —— 提供 DNS 报文二进制数据的读写操作
 *
 * 用于 DNS 协议报文的构建（写入）和解析（读取），支持：
 *   - 大端序多字节整数的读写（u8/u16/u32）
 *   - 原始字节数组的写入（用于资源记录的 RDATA 字段）
 *   - 动态扩容（写入时自动扩展缓冲区大小）
 *   - 边界安全校验（读取时溢出会抛出异常）
 *   - 指定位置写入（用于事后修正报文中的计数字段）
 *
 * DNS 协议所有多字节整数均采用网络字节序（大端序），因此：
 *   写入时：u16 高字节在先，低字节在后
 *   读取时：先读高字节，左移 8 位，再组合低字节
 */
#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>

class ByteBuffer {
public:
    // -----------------------------------------------------------------------
    // 构造 / 初始容量设置 —— 默认 512 字节（DNS 最小报文大小）
    // -----------------------------------------------------------------------
    explicit ByteBuffer(size_t capacity = 512) : buf_(capacity), pos_(0) {}

    // 从已有数据构造 —— 常用于从接收到的原始报文创建缓冲区以解析
    ByteBuffer(const uint8_t* data, size_t len) : buf_(data, data + len), pos_(0) {}

    // -----------------------------------------------------------------------
    // 读取操作 —— 每读一个值，pos_ 自动后移对应字节数
    // -----------------------------------------------------------------------

    /** 读取 1 字节无符号整数 */
    uint8_t read_u8()  { check(1); return buf_[pos_++]; }

    /** 读取 2 字节大端序无符号整数（例如 QTYPE、QCLASS） */
    uint16_t read_u16() {
        check(2);
        uint16_t v = (uint16_t)buf_[pos_] << 8 | buf_[pos_+1];
        pos_ += 2;
        return v;
    }

    /** 读取 4 字节大端序无符号整数（例如 TTL） */
    uint32_t read_u32() {
        check(4);
        uint32_t v = (uint32_t)buf_[pos_]<<24 | (uint32_t)buf_[pos_+1]<<16
                   | (uint32_t)buf_[pos_+2]<<8 | buf_[pos_+3];
        pos_ += 4;
        return v;
    }

    // -----------------------------------------------------------------------
    // 写入操作 —— 缓冲区不足时自动扩容（ensure）
    // -----------------------------------------------------------------------

    void write_u8(uint8_t v)    { ensure(1); buf_[pos_++] = v; }
    void write_u16(uint16_t v)  { ensure(2); buf_[pos_++] = v >> 8; buf_[pos_++] = v & 0xFF; }
    void write_u32(uint32_t v)  { ensure(4); buf_[pos_++]=v>>24; buf_[pos_++]=(v>>16)&0xFF; buf_[pos_++]=(v>>8)&0xFF; buf_[pos_++]=v&0xFF; }

    /** 写入原始字节数组 —— 例如资源记录的 RDATA 数据 */
    void write_bytes(const uint8_t* data, size_t len) {
        ensure(len);
        std::memcpy(&buf_[pos_], data, len);
        pos_ += len;
    }

    // -----------------------------------------------------------------------
    // 位置控制 —— 跳过字节、重设位置、查询位置
    // -----------------------------------------------------------------------

    void skip(size_t n)     { check(n); pos_ += n; }
    void set_pos(size_t p)  { if (p > buf_.size()) throw std::out_of_range("ByteBuffer::set_pos"); pos_ = p; }
    size_t pos() const      { return pos_; }
    size_t size() const     { return buf_.size(); }
    const uint8_t* data() const { return buf_.data(); }
    uint8_t* data()         { return buf_.data(); }

    /**
     * 在指定位置写入 2 字节 —— 用于报文构建完成后修正头部计数字段
     * 例如：在构建响应报文时，先预留位置，事后填入 ans_count 的实际值
     */
    void write_u16_at(size_t offset, uint16_t v) {
        if (offset + 2 > buf_.size()) throw std::out_of_range("ByteBuffer::write_u16_at");
        buf_[offset] = v >> 8;
        buf_[offset+1] = v & 0xFF;
    }

private:
    // 读取前校验边界 —— 不足则抛出异常，防止越界访问
    void check(size_t n) const {
        if (pos_ + n > buf_.size()) throw std::out_of_range("ByteBuffer read overflow");
    }

    // 写入前校验空间 —— 不足则自动扩容
    void ensure(size_t n) {
        if (pos_ + n > buf_.size()) buf_.resize(pos_ + n);
    }

    std::vector<uint8_t> buf_;   // 底层存储缓冲区
    size_t pos_;                 // 当前读写位置指针
};
