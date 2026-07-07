/**
 * @file udp_server.cpp
 * @brief UDP 服务器实现 —— 桥接事件循环和查询处理逻辑
 *
 * UdpServer 是网络层和应用层之间的适配器:
 *   1. 通过 IIoLoop 工厂创建平台相关的事件循环
 *   2. 绑定指定地址端口的 UDP 套接字
 *   3. 收到数据报 → 调用用户提供的 QueryHandler 处理 → 将响应发回发送方
 *
 * 请求-响应流程:
 *   客户端发送 DNS 查询 → loop_ 收到 UDP 数据报 → handler_ 解析并处理
 *   → 生成 DNS 响应报文 → loop_->send_udp() 发送回客户端
 */
#include "udp_server.h"
#include "io_loop.h"

/** 构造时创建平台相关的事件循环（IOCP 或 epoll） */
UdpServer::UdpServer() : loop_(create_io_loop()) {}

/** 析构函数定义在此处 —— 此时 IIoLoop 已是完整类型，unique_ptr 可正确释放 */
UdpServer::~UdpServer() = default;

bool UdpServer::start(const std::string& address, uint16_t port, QueryHandler handler) {
    if (!loop_->init()) return false;                    // 初始化平台（WSAStartup / epoll_create）
    if (!loop_->bind_udp(address.c_str(), port)) return false; // 绑定 UDP 端口

    handler_ = std::move(handler);                       // 保存查询处理回调

    // 启动事件循环（阻塞当前线程），使用函数对象作为回调
    struct UdpCallback { UdpServer* s; void operator()(UdpEvent&& ev) { s->on_recv(std::move(ev)); } };
    loop_->run(UdpCallback{this});
    return true;
}

void UdpServer::on_recv(UdpEvent&& ev) {
    auto resp = handler_(ev.data, ev.sender_ip);
    if (!resp.empty()) {
        loop_->send_udp(ev.sender_ip, ev.sender_port, resp);
    }
}

void UdpServer::stop() { loop_->stop(); }
