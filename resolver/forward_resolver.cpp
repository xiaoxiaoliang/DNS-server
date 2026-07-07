/**
 * @file forward_resolver.cpp
 * @brief 上游 DNS 转发实现 —— 带超时重试和故障转移
 *
 * 转发流程:
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 获取 Round-Robin 索引 → 遍历所有上游服务器                    │
 *   │   └─ 对每台服务器: 尝试 send_query (最多 max_retries+1 次)    │
 *   │       ├─ 成功 → 返回响应                                     │
 *   │       └─ 失败 → 重试或切换到下一台服务器                      │
 *   │ 全部失败 → 返回 nullopt (上层生成 SERVFAIL)                   │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * SO_RCVTIMEO 的作用:
 *   设置 Socket 接收超时后，recvfrom 在指定时间内无数据到达时
 *   返回 -1/EWOULDBLOCK，避免线程永久阻塞在网络等待上。
 *
 *   之所以每次创建新 Socket 而非复用:
 *   - 避免并发冲突（多个查询共享同一个 Socket 需要额外的包匹配逻辑）
 *   - 超时设置互不影响
 *   - DNS 查询通常是低频操作（有缓存兜底），创建 Socket 开销可忽略
 */
#include "forward_resolver.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// MSVC 不提供 ssize_t，手动定义
typedef SSIZE_T ssize_t;
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#endif

#include "../common/logger.h"

ForwardResolver::ForwardResolver(int timeout_sec, int max_retries)
    : timeout_sec_(timeout_sec), max_retries_(max_retries) {}

void ForwardResolver::add_server(const std::string& ip, uint16_t port) {
    servers_.push_back({ip, port});
}

optional<std::vector<uint8_t>> ForwardResolver::forward(const std::vector<uint8_t>& query) {
    // -------------------------------------------------------------------
    // 防护: 无可用上游服务器时直接返回失败
    // -------------------------------------------------------------------
    if (servers_.empty()) {
        LOG_ERROR("No upstream DNS servers configured");
        return nullopt;
    }

    // -------------------------------------------------------------------
    // Round-Robin: 从上次使用的服务器的下一台开始
    // -------------------------------------------------------------------
    size_t start_idx = round_robin_idx_++ % servers_.size();

    // -------------------------------------------------------------------
    // 遍历所有上游服务器 —— 直到某台成功返回或全部尝试完毕
    // -------------------------------------------------------------------
    for (size_t server_try = 0; server_try < servers_.size(); ++server_try) {
        size_t idx = (start_idx + server_try) % servers_.size();

        // 对当前服务器进行 retry 次尝试
        for (int retry = 0; retry <= max_retries_; ++retry) {
            auto result = send_query(servers_[idx], query);
            if (result.has_value()) {
                LOG_DEBUG("Forward success via " + servers_[idx].ip + ":" +
                          std::to_string(servers_[idx].port));
                return result;                            // 成功，返回结果
            }

            if (retry < max_retries_) {
                LOG_WARN("Forward retry " + std::to_string(retry + 1) + " to " +
                         servers_[idx].ip);
            }
        }

        // 当前服务器所有重试均失败，尝试下一台
        LOG_WARN("Forward failed via " + servers_[idx].ip + ":" +
                 std::to_string(servers_[idx].port) + ", trying next server");
    }

    // 所有上游 DNS 服务器均失败
    LOG_ERROR("All upstream DNS servers failed, returning nullopt");
    return nullopt;
}

// ============================================================================
// 单次查询 —— 创建临时 Socket 发送并接收 DNS 报文
// ============================================================================
optional<std::vector<uint8_t>> ForwardResolver::send_query(const UpstreamServer& server,
                                                                 const std::vector<uint8_t>& query) {
    // RAII socket 守卫：确保任何退出路径都关闭 socket
    struct SockGuard {
#ifdef _WIN32
        SOCKET s;
        SockGuard(SOCKET sock) : s(sock) {}
        ~SockGuard() { if (s != INVALID_SOCKET) closesocket(s); }
#else
        int s;
        SockGuard(int sock) : s(sock) {}
        ~SockGuard() { if (s >= 0) close(s); }
#endif
    };

    // -------------------------------------------------------------------
    // 创建 UDP Socket
    // -------------------------------------------------------------------
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) return nullopt;
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return nullopt;
#endif
    SockGuard guard(sock);  // 从此之后任何路径都会自动 closesocket

    // -------------------------------------------------------------------
    // 设置接收超时 —— 超时后 recvfrom 返回 -1/SOCKET_ERROR
    // Windows: DWORD 毫秒; Linux: struct timeval 秒+微秒
    // -------------------------------------------------------------------
#ifdef _WIN32
    DWORD tv = timeout_sec_ * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_sec_;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    // -------------------------------------------------------------------
    // 构造目标地址并发送查询
    // -------------------------------------------------------------------
    sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(server.port);                         // 端口转网络字节序
    inet_pton(AF_INET, server.ip.c_str(), &dst.sin_addr);     // IP 字符串转二进制

    // 发送 DNS 查询报文
    ssize_t sent = sendto(sock, (const char*)query.data(), (int)query.size(), 0,
                         (sockaddr*)&dst, sizeof(dst));
    if (sent != (ssize_t)query.size()) {  // 发送不完整 → 失败
        return nullopt;
    }

    // -------------------------------------------------------------------
    // 接收响应 —— 最大 4096 字节（支持 EDNS 扩展）
    // -------------------------------------------------------------------
    uint8_t buf[4096];
    sockaddr_in from = {};
#ifdef _WIN32
    int from_len = sizeof(from);
#else
    socklen_t from_len = sizeof(from);
#endif
    ssize_t n = recvfrom(sock, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &from_len);

    if (n <= 0) return nullopt;   // 超时或错误
    return std::vector<uint8_t>(buf, buf + n);  // 返回完整的响应报文
}
