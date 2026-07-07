/**
 * @file main.cpp
 * @brief DNS 服务器主入口 —— 初始化和启动所有模块
 *
 * 启动流程:
 *   1. 加载配置文件 (config/dns.conf)
 *   2. 初始化日志系统
 *   3. 注册信号处理器 (Ctrl+C 优雅退出)
 *   4. 加载 hosts 文件
 *   5. 加载黑名单和白名单
 *   6. 初始化 LRU 缓存
 *   7. 初始化上游 DNS 转发器
 *   8. 创建查询调度器（串联所有解析器）
 *   9. 启动统计报告线程（周期性输出运行指标）
 *   10. 启动 UDP 服务器（进入事件循环，阻塞主线程）
 *   11. 收到退出信号后清理资源
 *
 * 退出方式:
 *   - Windows: Ctrl+C
 *   - Linux:   Ctrl+C 或 kill -SIGTERM
 *
 * 使用方式:
 *   dns-server [配置文件路径]
 *   默认配置文件: config/dns.conf
 *   DNS 监听端口默认 53（需管理员/root 权限），测试可用 5353
 */
#include <iostream>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include "common/logger.h"
#include "common/types.h"
#include "config/config_loader.h"
#include "dns/dns_parser.h"
#include "dns/dns_builder.h"
#include "network/udp_server.h"
#include "resolver/hosts_resolver.h"
#include "resolver/filter_resolver.h"
#include "resolver/cache_resolver.h"
#include "resolver/forward_resolver.h"
#include "resolver/query_dispatcher.h"
#include "monitor/stats.h"

// ============================================================================
// 全局指针 —— 供信号处理器访问以优雅关闭
// ============================================================================
static UdpServer* g_server = nullptr;          // UDP 服务器
static CacheResolver* g_cache = nullptr;       // DNS 缓存（供统计线程清理过期条目）
static HostsResolver* g_hosts = nullptr;       // Hosts 解析器
static FilterResolver* g_filter = nullptr;     // 域名过滤器

// ============================================================================
// 信号处理器 —— 捕获 Ctrl+C 和 SIGTERM 实现优雅退出
// ============================================================================
#ifdef _WIN32
/** Windows 控制台事件处理 —— Ctrl+C / 控制台关闭 */
static BOOL WINAPI console_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
        LOG_INFO("Shutting down...");
        if (g_server) g_server->stop();
        return TRUE;
    }
    return FALSE;
}

/** 崩溃转储 —— 未处理异常时自动生成 .dmp 文件供事后分析 */
static LONG WINAPI crash_dump_handler(EXCEPTION_POINTERS* ex_info) {
    char filename[MAX_PATH];
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(filename, sizeof(filename),
             "crash_%04d%02d%02d_%02d%02d%02d.dmp",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileA(filename, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ex_info;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile, MiniDumpNormal, &mei, nullptr, nullptr);
        CloseHandle(hFile);

        std::cerr << "[CRASH] Minidump saved: " << filename << std::endl;
    }
    return EXCEPTION_EXECUTE_HANDLER; // 让系统继续默认处理
}
#else
/** Linux 信号处理 —— SIGINT / SIGTERM */
static void signal_handler(int) {
    LOG_INFO("Shutting down...");
    if (g_server) g_server->stop();
}
#endif

// ============================================================================
// 函数对象 —— 替代 lambda，用于统计线程和 DNS 查询处理
// ============================================================================

/** 统计报告线程的入口函数对象 */
struct StatsRunner {
    std::atomic<bool>& running;
    CacheResolver& cache;
    int interval;
    void operator()() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(interval));
            if (!running) break;
            Stats::instance().report();
            size_t evicted = cache.evict_expired();
            if (evicted > 0) {
                LOG_DEBUG("Evicted " + std::to_string(evicted) + " expired cache entries");
            }
        }
    }
};

/** DNS 查询处理器 —— 替代 lambda，每次收到查询报文时调用 */
struct DnsQueryHandler {
    QueryDispatcher& dispatcher;
    std::vector<uint8_t> operator()(const std::vector<uint8_t>& query, const std::string& /*sender_ip*/) {
        Stats::instance().inc_total();
        if (query.size() < DnsHeader::SIZE || query.size() > 4096) return {};
        auto parsed = DnsParser::parse_query(query);
        if (!parsed.valid) return {};
        return dispatcher.process(query);
    }
};

// ============================================================================
// 解析上游 DNS 服务器配置字符串
// 格式: "ip:port,ip:port,..."
// 例如: "8.8.8.8:53,114.114.114.114:53"
// ============================================================================
static std::vector<std::pair<std::string, uint16_t>> parse_upstream(const std::string& s) {
    std::vector<std::pair<std::string, uint16_t>> result;
    size_t start = 0;

    // 按逗号分割: "8.8.8.8:53,114.114.114.114:53" → ["8.8.8.8:53", "114.114.114.114:53"]
    while (start < s.size()) {
        auto comma = s.find(',', start);
        std::string item = s.substr(start, comma - start);

        // 按冒号分割 IP 和端口: "8.8.8.8:53" → IP="8.8.8.8", PORT=53
        auto colon = item.find(':');
        std::string ip = item.substr(0, colon);
        ip.erase(0, ip.find_first_not_of(" \t"));        // 去除左侧空白
        ip.erase(ip.find_last_not_of(" \t") + 1);        // 去除右侧空白

        std::string port_str = (colon != std::string::npos) ? item.substr(colon + 1) : "53";
        uint16_t port = (uint16_t)std::stoul(port_str);  // 字符串转端口号

        result.emplace_back(ip, port);

        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return result;
}

// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char* argv[]) {
    // -------------------------------------------------------------------
    // 1. 加载配置文件
    //    - 命令行参数可指定配置文件路径
    //    - 默认路径: config/dns.conf
    // -------------------------------------------------------------------
    std::string config_path = "config/dns.conf";
    if (argc > 1) config_path = argv[1];

    ConfigLoader config;
    if (!config.load(config_path)) {
        std::cerr << "Failed to load config: " << config_path << std::endl;
        std::cerr << "Usage: " << argv[0] << " [config_file]" << std::endl;
        return 1;
    }

    // -------------------------------------------------------------------
    //  Linux daemon 化 —— 脱离终端在后台运行
    // -------------------------------------------------------------------
#ifndef _WIN32
    if (daemon(0, 0) != 0) return 1;
#endif

    // -------------------------------------------------------------------
    // 2. 初始化日志系统
    //    log_level: 日志级别 (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)
    //    log_file:  日志文件路径 (为空则输出到控制台)
    // -------------------------------------------------------------------
    Logger::instance().init(
        static_cast<LogLevel>(config.get_int("log_level", 1)),
        config.get("log_file", "")
    );

    LOG_INFO("DNS Server starting...");
    LOG_INFO("Config loaded from: " + config_path);

    // -------------------------------------------------------------------
    // 3. 注册信号处理器 —— 实现 Ctrl+C 优雅退出
    // -------------------------------------------------------------------
#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
    SetUnhandledExceptionFilter(crash_dump_handler);   // 崩溃时自动写 .dmp 文件
#else
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#endif

    // -------------------------------------------------------------------
    // 4. 加载 hosts 文件
    // -------------------------------------------------------------------
    HostsResolver hosts;
    std::string hosts_file = config.get("hosts_file", "config/hosts.txt");
    g_hosts = &hosts;
    hosts.load(hosts_file);

    // -------------------------------------------------------------------
    // 5. 加载黑名单和白名单
    // -------------------------------------------------------------------
    FilterResolver filter;
    g_filter = &filter;
    std::string blocklist_file = config.get("blocklist_file", "config/blocklist.txt");
    std::string whitelist_file = config.get("whitelist_file", "config/whitelist.txt");
    filter.load_blocklist(blocklist_file);
    filter.load_whitelist(whitelist_file);

    // -------------------------------------------------------------------
    // 6. 初始化 LRU 缓存
    // -------------------------------------------------------------------
    CacheResolver cache(config.get_int("cache_max_entries", 10000));
    g_cache = &cache;

    // -------------------------------------------------------------------
    // 7. 初始化上游 DNS 转发器
    //    默认上游: Google DNS (8.8.8.8) + 114 DNS (114.114.114.114)
    // -------------------------------------------------------------------
    ForwardResolver forward(
        config.get_int("forward_timeout", 5),       // 单次查询超时: 5 秒
        config.get_int("forward_max_retries", 2)    // 每台服务器最多重试: 2 次
    );

    auto upstream = parse_upstream(config.get("upstream_servers",
        "8.8.8.8:53,114.114.114.114:53"));
    for (auto& server : upstream) {
        forward.add_server(server.first, server.second);
        LOG_INFO("Upstream DNS: " + server.first + ":" + std::to_string(server.second));
    }

    // -------------------------------------------------------------------
    // 8. 创建查询调度器 —— 串联所有解析器形成解析链
    // -------------------------------------------------------------------
    QueryDispatcher dispatcher(&hosts, &filter, &cache, &forward);

    // -------------------------------------------------------------------
    // 9. 启动统计报告线程 —— 每 stats_interval 秒输出运行指标并清理过期缓存
    // -------------------------------------------------------------------
    int stats_interval = config.get_int("stats_interval", 300);  // 默认 5 分钟
    std::atomic<bool> stats_running{false};
    std::thread stats_thread;

    if (stats_interval > 0) {
        stats_running = true;
        stats_thread = std::thread(StatsRunner{stats_running, cache, stats_interval});
    }

    // -------------------------------------------------------------------
    // 10. 启动 UDP 服务器 —— 进入事件循环，阻塞主线程
    //     默认监听 0.0.0.0:53 (所有网卡，标准 DNS 端口)
    //     绑定 53 端口需要管理员权限，测试可改为 5353
    // -------------------------------------------------------------------
    std::string listen_addr = config.get("listen_address", "0.0.0.0");
    uint16_t listen_port = (uint16_t)config.get_int("listen_port", 53);

    UdpServer server;
    g_server = &server;

    LOG_INFO("Listening on " + listen_addr + ":" + std::to_string(listen_port));

    // 启动 UDP 事件循环 —— 处理每个到达的 DNS 查询报文
    server.start(listen_addr, listen_port, DnsQueryHandler{dispatcher});

    // -------------------------------------------------------------------
    // 11. 收到退出信号后清理
    // -------------------------------------------------------------------
    g_server = nullptr;
    // 先通知统计线程停止并等待其退出（避免访问已销毁的栈对象）
    if (stats_interval > 0 && stats_thread.joinable()) {
        stats_running = false;
        stats_thread.join();
    }
    LOG_INFO("DNS Server stopped.");

    return 0;
}
