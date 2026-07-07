# DNS-server

一个高性能、跨平台的 DNS 服务器，采用 **解析器链 (Chain of Responsibility)** 架构，支持本地 hosts 解析、域名黑白名单过滤、LRU 缓存和上游 DNS 转发。支持 **Windows** 和 **Linux** 双平台。

## 系统架构

```
┌─────────────────────────────────────────┐
│              UDP Server                  │
│         (IOCP / epoll 事件循环)           │
└────────────────┬────────────────────────┘
                 │ DNS 查询报文
                 ▼
┌─────────────────────────────────────────┐
│          Query Dispatcher                │
│         (解析器链调度器)                   │
├─────────────────────────────────────────┤
│  1. Hosts Resolver      (本地 hosts)     │
│  2. Filter Resolver     (黑白名单 Trie)   │
│  3. Cache Resolver      (LRU 缓存)       │
│  4. Forward Resolver    (上游 DNS)       │
└─────────────────────────────────────────┘
```

### 解析流程

1. **Hosts 解析** —— 查询本地 hosts 文件，命中直接返回
2. **黑白名单过滤** —— Trie 树匹配域名，命中黑名单返回 `0.0.0.0`，白名单优先放行
3. **LRU 缓存** —— 检查本地缓存，命中直接返回，未命中继续
4. **上游 DNS 转发** —— 向上游 DNS 服务器发起查询，结果写入缓存后返回
5. 所有阶段均失败 → 返回 SERVFAIL

## 项目结构

```
DNS-server/
├── main.cpp                   # 主入口 —— 初始化和启动所有模块
├── Makefile                   # Linux 编译 (g++)
├── DNS-server.sln             # Windows 编译 (Visual Studio)
├── config/
│   ├── config_loader.h/.cpp   # INI 风格配置文件加载器
│   └── ...                    # 运行时配置文件 (dns.conf 等)
├── common/
│   ├── logger.h/.cpp          # 线程安全日志系统 (毫秒时间戳)
│   ├── types.h                # DNS 协议枚举 (RRType, RCode 等)
│   ├── optional.h             # C++11 兼容 optional<T>
│   └── buffer.h               # 字节缓冲区工具
├── dns/
│   ├── dns_parser.h           # DNS 报文解析 (域名压缩指针)
│   ├── dns_builder.h/.cpp     # DNS 响应报文构造
│   ├── dns_header.h           # DNS 头部 (12 字节)
│   ├── dns_question.h         # DNS 问题段
│   └── dns_rr.h               # DNS 资源记录
├── network/
│   ├── udp_server.h/.cpp      # UDP 服务器 (跨平台封装)
│   ├── io_loop.h              # I/O 事件循环抽象接口
│   ├── platform_loop.cpp      # 平台实现 (IOCP / epoll)
│   └── platform_loop.h        # 平台实现头文件
├── resolver/
│   ├── query_dispatcher.h/.cpp    # 查询调度器 (解析器链)
│   ├── hosts_resolver.h/.cpp      # Hosts 文件解析
│   ├── filter_resolver.h/.cpp     # 域名过滤器 (Trie 树)
│   ├── cache_resolver.h/.cpp      # LRU + TTL 缓存
│   └── forward_resolver.h/.cpp    # 上游 DNS 转发
└── monitor/
    └── stats.h/.cpp           # 运行时统计监控
```

## 快速开始

### 环境要求

| 平台 | 编译器 | 依赖 |
|------|--------|------|
| Linux | GCC 4.8+ | pthread |
| Windows | MSVC 2015+ | WinSock2 |
| macOS | Clang 3.3+ | pthread |

### Linux 编译运行

```bash
# 编译 (Debug)
make

# 编译 (Release)
make release

# 清理
make clean

# 运行 (默认监听 53 端口，需要 root)
sudo ./Build/DNS-server

# 指定配置文件
sudo ./Build/DNS-server /path/to/dns.conf

# 测试 (使用 5353 端口，不需要 root)
sudo ./Build/DNS-server config/test.conf
```

### Windows 编译运行

使用 Visual Studio 打开 `DNS-server.sln` 编译，或通过 MSVC 命令行：

```bash
# MSVC 编译
cl /EHsc /std:c++11 /Fe:DNS-server.exe main.cpp *.cpp config/*.cpp ...

# 运行 (需管理员权限)
DNS-server.exe config/dns.conf
```

### macOS 编译运行

```bash
# macOS 也使用 Makefile
make
sudo ./Build/DNS-server
```

## 配置说明

配置文件为 INI 格式，默认路径 `config/dns.conf`：

```ini
# 监听地址和端口
listen_address = 0.0.0.0
listen_port = 53

# 上游 DNS 服务器 (逗号分隔，支持轮询)
upstream_servers = 8.8.8.8:53,114.114.114.114:53

# 转发超时 (秒) 和单台服务器最大重试次数
forward_timeout = 5
forward_max_retries = 2

# 缓存最大条目数
cache_max_entries = 10000

# Hosts 文件路径
hosts_file = hosts.txt

# 黑名单/白名单文件路径
blocklist_file = config/blocklist.txt
whitelist_file = config/whitelist.txt

# 日志级别: 0=DEBUG 1=INFO 2=WARN 3=ERROR
log_level = 1
log_file = dns-server.log

# 统计报告间隔 (秒), 0 禁用
stats_interval = 300
```

### Hosts 文件格式

```
# 注释行
192.168.1.100    internal.example.com
10.0.0.1         local.dev
::1              ipv6.local
```

### 黑白名单格式

```
# 每行一个域名
doubleclick.net          # 精确匹配
*.googleadservices.com   # 通配符 (匹配所有子域名)
```

## 测试

```bash
# Linux 测试
dig @127.0.0.1 -p 53 www.example.com

# 或使用 nslookup
nslookup www.example.com 127.0.0.1

# 查询 AAAA 记录
dig @127.0.0.1 -p 53 AAAA www.example.com
```

## 统计指标

服务器运行时每 `stats_interval` 秒输出一次统计报告：

| 指标 | 说明 |
|------|------|
| total_queries | 总查询数 |
| cache_hits / cache_misses | 缓存命中/未命中数 |
| cache_hit_rate | 缓存命中率 (%) |
| blocked | 黑名单拦截数 |
| forwarded | 上游转发数 |
| forward_failures | 转发失败数 |
| QPS | 每秒查询数 (增量) |

## 许可证

Apache License 2.0 — 详见 [LICENSE.txt](LICENSE.txt)
