/**
 * @file forward_resolver.h
 * @brief 上游 DNS 转发器 —— 将无法本地解析的查询转发到上游 DNS 服务器
 *
 * 转发策略:
 *   - Round-Robin 轮询: 在多个上游 DNS 间轮转，实现负载分散
 *   - 超时重试: 单台服务器可重试 max_retries 次
 *   - 故障转移: 一台服务器全部失败后自动切换到下一台
 *   - 全部失败后返回 nullopt，由上层（QueryDispatcher）生成 SERVFAIL 响应
 *
 * 每个请求创建独立的临时 UDP Socket，设置 SO_RCVTIMEO 超时，
 * 避免影响并发查询和事件循环。
 */
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../common/optional.h"

/** 上游 DNS 服务器配置 */
struct UpstreamServer {
    std::string ip;       // IP 地址 (点分十进制)
    uint16_t port;        // 端口号 (通常为 53)
};

class ForwardResolver {
public:
    /**
     * 构造函数
     * @param timeout_sec   单次查询超时秒数（默认 5 秒）
     * @param max_retries   单台服务器最大重试次数（默认 2 次，即共 3 次尝试）
     */
    explicit ForwardResolver(int timeout_sec = 5, int max_retries = 2);

    /** 添加一台上游 DNS 服务器 */
    void add_server(const std::string& ip, uint16_t port);

    /**
     * 转发 DNS 查询到上游
     * @param query  原始 DNS 查询报文
     * @return       成功 → DNS 响应报文; 全部服务器失败 → nullopt
     */
    optional<std::vector<uint8_t>> forward(const std::vector<uint8_t>& query);

private:
    /**
     * 向单台上游服务器发送查询并接收响应
     * 创建临时 UDP Socket → 设置超时 → sendto → recvfrom → 关闭 Socket
     */
    optional<std::vector<uint8_t>> send_query(const UpstreamServer& server,
                                                    const std::vector<uint8_t>& query);

    std::vector<UpstreamServer> servers_;   // 上游服务器列表
    int timeout_sec_;                        // 单次查询超时（秒）
    int max_retries_;                        // 单台服务器最大重试次数
    size_t round_robin_idx_ = 0;             // Round-Robin 轮询索引
};
