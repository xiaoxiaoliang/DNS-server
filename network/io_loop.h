/**
 * @file io_loop.h
 * @brief 跨平台 I/O 事件循环抽象接口
 *
 * 定义平台无关的异步网络事件处理接口，分离"做什么"与"怎么做"。
 * Windows 实现: IOCP (I/O Completion Ports)  —— 关联到完成端口，异步接收
 * Linux 实现:   epoll (边缘触发模式 + 非阻塞 Socket)
 *
 * 事件模型:
 *   init() → bind_udp() → run(callback)   ←  启动并进入事件循环
 *       ↑                                    ←  send_udp() 可在任意时刻调用
 *   stop()                                    ←  优雅退出事件循环
 */
#pragma once
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <memory>

/** UDP 接收事件 —— 一次完整的 UDP 数据报 */
struct UdpEvent {
    std::vector<uint8_t> data;   // 接收到的原始报文数据
    std::string          sender_ip;    // 发送方 IP 地址 (点分十进制)
    uint16_t             sender_port;  // 发送方端口号
};

class IIoLoop {
public:
    /** 接收回调类型 —— 收到数据报时调用 */
    using RecvCallback = std::function<void(UdpEvent&& event)>;

    virtual ~IIoLoop() = default;

    /** 初始化平台 —— Windows 下调用 WSAStartup，Linux 下创建 epoll fd */
    virtual bool init() = 0;

    /** 绑定 UDP 套接字到指定地址和端口，并注册到事件通知机制 */
    virtual bool bind_udp(const char* address, uint16_t port) = 0;

    /** 发送 UDP 数据报到目标地址 */
    virtual bool send_udp(const std::string& dst_ip, uint16_t dst_port,
                          const std::vector<uint8_t>& data) = 0;

    /** 启动事件循环（阻塞当前线程），收到数据时通过回调通知上层 */
    virtual void run(const RecvCallback& on_recv) = 0;

    /** 停止事件循环 —— 设置退出标志，run() 将结束阻塞 */
    virtual void stop() = 0;
};

/**
 * 工厂函数 —— 根据编译平台创建对应的 IIoLoop 实现
 * Windows → IocpLoop（完成端口）
 * Linux   → EpollLoop（epoll + 边缘触发）
 */
std::unique_ptr<IIoLoop> create_io_loop();
