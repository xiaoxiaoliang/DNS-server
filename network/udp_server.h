/**
 * @file udp_server.h
 * @brief UDP 服务器 —— 封装跨平台 I/O 事件循环，提供简洁的 DNS 查询处理接口
 *
 * UdpServer 作为网络层的最外层封装:
 *   - 内部通过 create_io_loop() 创建平台专用的事件循环
 *   - 提供 start(addr, port, handler) 一键启动接口
 *   - handler 接收原始查询报文和发送方 IP，返回响应报文
 *   - 自动将响应发送回客户端
 *
 * 使用方式:
 *   UdpServer server;
 *   server.start("0.0.0.0", 53, [&](auto& query, auto& ip) {
 *       return dispatcher.process(query);
 *   });
 */
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include "io_loop.h"

class UdpServer {
public:
    /**
     * 查询处理器类型
     * @param query     原始 DNS 查询报文
     * @param sender_ip 查询来源 IP 地址（点分十进制）
     * @return          需要返回给客户端的 DNS 响应报文（空 vector 表示不回复）
     */
    using QueryHandler = std::function<std::vector<uint8_t>(
        const std::vector<uint8_t>& query, const std::string& sender_ip)>;

    UdpServer();
    ~UdpServer();   // 定义在 .cpp 中，确保 IIoLoop 完整类型可见

    /**
     * 启动 UDP 服务器并进入事件循环（阻塞当前线程）
     * @param address  监听地址（"0.0.0.0" 表示所有网卡）
     * @param port     监听端口（53 为标准 DNS 端口，需管理员权限）
     * @param handler  DNS 查询处理回调
     * @return         true 成功启动，false 初始化失败
     */
    bool start(const std::string& address, uint16_t port, QueryHandler handler);

    /** 停止服务器 —— 退出事件循环 */
    void stop();

private:
    void on_recv(UdpEvent&& ev);       // 收到数据包时的处理函数
    std::unique_ptr<IIoLoop> loop_;    // 平台相关的事件循环 (IOCP / epoll)
    QueryHandler handler_;             // 查询处理回调
};
