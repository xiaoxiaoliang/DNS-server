/**
 * @file stats.h
 * @brief 运行时统计监控 —— 收集 DNS 服务器的运行指标
 *
 * 所有计数器使用 std::atomic<size_t> 实现无锁计数，无需额外加锁。
 *
 * 统计指标:
 *   - total_queries:   处理的总查询数
 *   - cache_hits:      缓存命中次数
 *   - cache_misses:    缓存未命中次数
 *   - blocked:         被黑名单拦截的查询数
 *   - forwarded:       转发到上游 DNS 的查询数
 *   - forward_failures: 上游转发失败次数
 *   - QPS:             每秒查询数（基于两次 report 之间的增量）
 *   - cache_hit_rate:  缓存命中率（百分比）
 *
 * 单例模式实现: 仅通过 instance() 访问全局唯一实例
 */
#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>

class Stats {
public:
    /** 获取单例实例 */
    static Stats& instance();

    void inc_total();          // 总查询数 +1
    void inc_cache_hit();      // 缓存命中 +1
    void inc_cache_miss();     // 缓存未命中 +1
    void inc_blocked();        // 拦截数 +1
    void inc_forwarded();      // 转发数 +1
    void inc_forward_fail();   // 转发失败 +1

    /**
     * 缓存命中率 —— 百分比 (0~100)
     * total_queries == 0 时返回 0
     */
    double cache_hit_rate() const;

    /**
     * 输出统计报告到日志 —— 显示所有计数器和 QPS 信息
     * QPS 计算: (本次总查询 - 上次总查询) / 两次报告间隔秒数
     */
    void report();

private:
    Stats();  // 构造函数私有

    std::atomic<size_t> total_queries_{0};      // 总查询数
    std::atomic<size_t> cache_hits_{0};         // 缓存命中数
    std::atomic<size_t> cache_misses_{0};        // 缓存未命中数
    std::atomic<size_t> blocked_{0};             // 被拦截数
    std::atomic<size_t> forwarded_{0};           // 转发数
    std::atomic<size_t> forward_failures_{0};    // 转发失败数
    std::chrono::steady_clock::time_point last_report_;  // 上次报告时间点
    size_t last_total_ = 0;                     // 上次报告时的总查询数
};
