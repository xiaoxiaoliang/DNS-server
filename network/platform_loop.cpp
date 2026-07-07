/**
 * @file platform_loop.cpp
 * @brief 跨平台事件循环实现 —— 通过 #ifdef 在同一文件中实现 Windows 和 Linux 版本
 *
 * ========== Windows IOCP 版本 ==========
 * 使用 Overlapped I/O + I/O Completion Port 实现异步 UDP 接收。
 *
 * 工作流程:
 *   1. 创建 UDP 套接字并绑定端口
 *   2. 将套接字关联到 IOCP 完成端口
 *   3. 提交异步 WSARecvFrom → 当数据到达时完成端口收到通知
 *   4. GetQueuedCompletionStatus 获取完成事件 → 回调上层处理
 *   5. 处理完毕后重新提交异步接收（post_recv），形成循环
 *
 * IOCP 优势: Windows 下最高效的异步 I/O 模型，内核级事件通知。
 *
 * ========== Linux epoll 版本 ==========
 * 使用 epoll 边缘触发 (EPOLLET) + 非阻塞 Socket 实现高并发 UDP 处理。
 *
 * 工作流程:
 *   1. 创建非阻塞 UDP 套接字并绑定端口
 *   2. 将套接字加入 epoll 实例（监听 EPOLLIN 事件）
 *   3. epoll_wait 等待事件通知
 *   4. 收到通知后用 while 循环非阻塞 recvfrom 读完所有数据（直到 EAGAIN）
 *   5. 回调上层处理
 *
 * 边缘触发优势: 每次事件只通知一次，避免重复唤醒，CPU 利用率更高。
 * while 循环读取: 确保一次性处理完 Socket 缓冲区中的所有数据包。
 */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h>
#else
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#endif

#include "io_loop.h"
#include <cstring>
#include <stdexcept>
#include <atomic>
#include <iostream>

// ============================================================================
// Windows IOCP 实现
// ============================================================================
#ifdef _WIN32

class IocpLoop : public IIoLoop {
    /** Overlapped 接收请求 —— 每次异步接收对应一个 OverlappedRecv 对象 */
    struct OverlappedRecv {
        OVERLAPPED ov;         // Windows 重叠 I/O 结构
        WSABUF wsabuf;         // 接收缓冲区描述
        uint8_t buf[4096];     // 实际接收缓冲区（最大 4096 字节，支持 EDNS）
        sockaddr_in from;      // 记录发送方地址
        int from_len;          // 地址结构长度
    };

public:
    ~IocpLoop() override {
        stop();
        if (sock_ != INVALID_SOCKET) closesocket(sock_);
        if (iocp_) CloseHandle(iocp_);
        WSACleanup();          // 清理 Winsock 库
    }

    /** 初始化 Winsock 并创建完成端口 */
    bool init() override {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        return iocp_ != nullptr;
    }

    /**
     * 绑定 UDP 端口并关联到完成端口
     * 创建套接字 → bind → 关联 IOCP → 提交首次异步接收
     */
    bool bind_udp(const char* address, uint16_t port) override {
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == INVALID_SOCKET) return false;

        // 禁用 UDP ICMP CONNRESET: 防止 sendto 发往已关闭端口后，
        // 后续 WSARecvFrom 因 ICMP Port Unreachable 返回 10054 错误
        BOOL false_flag = FALSE;
        DWORD bytes_ret = 0;
        WSAIoctl(sock_, SIO_UDP_CONNRESET, &false_flag, sizeof(false_flag),
                 nullptr, 0, &bytes_ret, nullptr, nullptr);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, address, &addr.sin_addr);

        if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(sock_);
            return false;
        }

        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(port);
        inet_pton(AF_INET, address, &addr_.sin_addr);

        // 将套接字关联到完成端口 —— 内核收到数据后会向该端口投递完成通知
        CreateIoCompletionPort((HANDLE)sock_, iocp_, 0, 0);

        // 提交首次异步接收 —— 之后每次处理完一条消息都重新提交
        post_recv();
        return true;
    }

    /** 同步发送 UDP 数据报 —— 直接调用 sendto */
    bool send_udp(const std::string& dst_ip, uint16_t dst_port,
                  const std::vector<uint8_t>& data) override {
        sockaddr_in dst = {};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(dst_port);
        inet_pton(AF_INET, dst_ip.c_str(), &dst.sin_addr);

        int sent = sendto(sock_, (const char*)data.data(), (int)data.size(), 0,
                         (sockaddr*)&dst, sizeof(dst));
        return sent == (int)data.size();
    }

    /**
     * 事件循环主函数 —— 阻塞等待完成通知并处理
     * 超时 1 秒: 用于周期性检查 running_ 标志，实现优雅退出
     */
    void run(const RecvCallback& on_recv) override {
        callback_ = on_recv;
        running_ = true;

        while (running_) {
            try {
            DWORD bytes;
            ULONG_PTR key;
            OVERLAPPED* ov;

            // 阻塞等待完成通知，超时 1 秒
            BOOL ok = GetQueuedCompletionStatus(iocp_, &bytes, &key, &ov, 1000);
            if (!running_) break;

            // 收到有效的数据报
            if (ov && ok && bytes > 0) {
                auto* r = CONTAINING_RECORD(ov, OverlappedRecv, ov);
                // 防护：检查 bytes 不超过缓冲区
                if (bytes > sizeof(r->buf)) {
                    std::cerr << "[ERROR] Packet too large: " << bytes << " bytes" << std::endl;
                    delete r;
                    post_recv();
                    continue;
                }
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &r->from.sin_addr, ip, sizeof(ip));

                UdpEvent ev;
                ev.data.assign(r->buf, r->buf + bytes);
                ev.sender_ip = ip;
                ev.sender_port = ntohs(r->from.sin_port);

                delete r;
                on_recv(std::move(ev));
                post_recv();
            }
            } catch (const std::exception& e) {
                std::cerr << "[FATAL] " << e.what() << std::endl;
                post_recv();
            } catch (...) {
                std::cerr << "[FATAL] unknown exception" << std::endl;
                post_recv();
            }
        }

        running_ = false;
    }

    void stop() override { running_ = false; }

private:
    /**
     * 提交一次异步接收请求 —— 创建新的 OverlappedRecv 并用 WSARecvFrom 发起
     * 若 WSARecvFrom 返回非零且错误码不是 WSA_IO_PENDING（预期行为），则失败
     * 若返回 0 表示同步完成：直接处理数据后释放对象（UDP over IOCP 极少发生此情况）
     */
    void post_recv() {
        auto* r = new OverlappedRecv();
        std::memset(&r->ov, 0, sizeof(r->ov));
        r->wsabuf.buf = (CHAR*)r->buf;
        r->wsabuf.len = sizeof(r->buf);
        r->from_len = sizeof(r->from);

        DWORD flags = 0, bytes = 0;
        int ret = WSARecvFrom(sock_, &r->wsabuf, 1, &bytes, &flags,
                             (sockaddr*)&r->from, &r->from_len, &r->ov, nullptr);
        if (ret == 0) {
            // 同步完成：数据已填入 r->buf，IOCP 会将完成通知排队到完成端口。
            // 绝不能 delete r，否则后续 GetQueuedCompletionStatus 拿到悬空指针会崩溃。
            // run() 中的事件循环会正常处理此次完成通知。
        } else if (WSAGetLastError() != WSA_IO_PENDING) {
            int err = WSAGetLastError();
            std::cerr << "[ERROR] WSARecvFrom failed, err=" << err << std::endl;
            delete r;
        }
    }

    SOCKET sock_ = INVALID_SOCKET;   // UDP 套接字句柄
    HANDLE iocp_ = nullptr;          // IOCP 完成端口句柄
    sockaddr_in addr_{};             // 本地监听地址
    RecvCallback callback_;          // 接收回调函数
    std::atomic<bool> running_{false}; // 运行状态标志
};

// ============================================================================
// Linux epoll 实现
// ============================================================================
#else

class EpollLoop : public IIoLoop {
public:
    ~EpollLoop() override {
        stop();
        if (epoll_fd_ >= 0) close(epoll_fd_);
        if (sock_ >= 0) close(sock_);
    }

    /** 创建 epoll 实例 */
    bool init() override {
        epoll_fd_ = epoll_create1(0);         // 等价于 epoll_create 但标志为 0
        return epoll_fd_ >= 0;
    }

    /**
     * 绑定 UDP 端口并注册到 epoll（边缘触发模式 + 非阻塞）
     *
     * EPOLLET (边缘触发): epoll 只在 Socket 状态变化时通知一次，之后需要循环读取
     * O_NONBLOCK:        Socket 设为非阻塞模式，recvfrom 在无数据时返回 EAGAIN
     * SO_REUSEADDR:      允许端口重用（避免重启时端口被占用）
     */
    bool bind_udp(const char* address, uint16_t port) override {
        sock_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ < 0) return false;

        // 设置为非阻塞模式
        int flags = fcntl(sock_, F_GETFL, 0);
        fcntl(sock_, F_SETFL, flags | O_NONBLOCK);

        int reuse = 1;
        setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, address, &addr.sin_addr);

        if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock_);
            return false;
        }

        // 注册到 epoll —— 边缘触发 + 只监听读事件
        epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = sock_;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_, &ev) < 0) {
            close(sock_);
            return false;
        }

        return true;
    }

    /** 同步发送 UDP 数据报 —— 使用 MSG_NOSIGNAL 避免 SIGPIPE */
    bool send_udp(const std::string& dst_ip, uint16_t dst_port,
                  const std::vector<uint8_t>& data) override {
        sockaddr_in dst = {};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(dst_port);
        inet_pton(AF_INET, dst_ip.c_str(), &dst.sin_addr);

        ssize_t sent = sendto(sock_, data.data(), data.size(), MSG_NOSIGNAL,
                             (sockaddr*)&dst, sizeof(dst));
        return sent == (ssize_t)data.size();
    }

    /**
     * 事件循环主函数 —— epoll_wait 等待事件 + while 循环一次性清空 Socket 缓冲区
     */
    void run(const RecvCallback& on_recv) override {
        running_ = true;
        std::vector<epoll_event> events(128);  // 一次最多处理 128 个事件

        while (running_) {
            // 等待事件通知，超时 100ms 用于周期性检查运行标志
            int n = epoll_wait(epoll_fd_, events.data(), (int)events.size(), 100);
            if (!running_) break;

            for (int i = 0; i < n; ++i) {
                if (events[i].data.fd == sock_) {
                    handle_read(on_recv);   // 处理 Socket 上的可读事件
                }
            }
        }
    }

    void stop() override { running_ = false; }

private:
    /**
     * 非阻塞循环读取 —— 边缘触发模式下必须用 while 循环读到底
     *
     * 边缘触发的关键理解:
     *   epoll 只通知一次"有新数据"事件，不会为每个数据包重复通知。
     *   因此必须在一个事件内用 while 循环将所有到达的数据包读完，
     *   否则未读取的数据包可能永远不会触发新的 epoll 事件。
     *
     *   循环终止条件: recvfrom 返回 EAGAIN/EWOULDBLOCK（Socket 缓冲区已空）
     */
    void handle_read(const RecvCallback& on_recv) {
        uint8_t buf[4096];
        while (running_) {
            sockaddr_in from = {};
            socklen_t from_len = sizeof(from);
            ssize_t n = recvfrom(sock_, buf, sizeof(buf), 0,
                                (sockaddr*)&from, &from_len);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 缓冲区已空，结束循环
                break;                                                // 其他错误，也退出
            }

            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));

            UdpEvent ev;
            ev.data.assign(buf, buf + n);
            ev.sender_ip = ip;
            ev.sender_port = ntohs(from.sin_port);

            on_recv(std::move(ev));
        }
    }

    int epoll_fd_ = -1;                  // epoll 文件描述符
    int sock_ = -1;                      // UDP 套接字文件描述符
    std::atomic<bool> running_{false};   // 运行状态标志
};

#endif

// ============================================================================
// 工厂函数 —— 根据平台编译标志创建对应的实现
// ============================================================================
std::unique_ptr<IIoLoop> create_io_loop() {
#ifdef _WIN32
    auto loop = std::unique_ptr<IIoLoop>(new IocpLoop());
#else
    auto loop = std::unique_ptr<IIoLoop>(new EpollLoop());
#endif
    return loop;
}
