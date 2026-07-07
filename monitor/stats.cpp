/**
 * @file stats.cpp
 * @brief 统计监控模块实现
 *
 * QPS (Queries Per Second) 计算方法:
 *   不是实时计算，而是基于两次 report() 调用之间的增量除以时间差，
 *   这样可以避免短时间内的剧烈波动，得到更平滑的平均 QPS 指标。
 */
#include "stats.h"
#include "../common/logger.h"
#include <chrono>

// ============================================================================
// 单例
// ============================================================================
Stats& Stats::instance() { static Stats s; return s; }

Stats::Stats() : last_report_(std::chrono::steady_clock::now()) {}

// ============================================================================
// 原子计数器 —— 无锁递增，保证多线程下的正确性
// ============================================================================
void Stats::inc_total()          { ++total_queries_; }
void Stats::inc_cache_hit()      { ++cache_hits_; }
void Stats::inc_cache_miss()     { ++cache_misses_; }
void Stats::inc_blocked()        { ++blocked_; }
void Stats::inc_forwarded()      { ++forwarded_; }
void Stats::inc_forward_fail()   { ++forward_failures_; }

double Stats::cache_hit_rate() const {
    size_t t = total_queries_;
    return t > 0 ? (double)cache_hits_ / t * 100.0 : 0.0;
}

// ============================================================================
// 统计报告 —— 输出所有运行指标到日志
// ============================================================================
void Stats::report() {
    auto now = std::chrono::steady_clock::now();

    // 计算自上次报告以来的平均 QPS
    double elapsed = std::chrono::duration<double>(now - last_report_).count();
    double qps = elapsed > 0 ? (total_queries_ - last_total_) / elapsed : 0;

    LOG_INFO("=== DNS Server Stats ===");
    LOG_INFO("  Total queries:   " + std::to_string(total_queries_));
    LOG_INFO("  Cache hits:      " + std::to_string(cache_hits_));
    LOG_INFO("  Cache misses:    " + std::to_string(cache_misses_));
    LOG_INFO("  Blocked:         " + std::to_string(blocked_));
    LOG_INFO("  Forwarded:       " + std::to_string(forwarded_));
    LOG_INFO("  Forward fails:   " + std::to_string(forward_failures_));
    LOG_INFO("  Cache hit rate:  " + std::to_string(cache_hit_rate()) + "%");
    LOG_INFO("  QPS (avg):       " + std::to_string(qps));

    // 记录本次报告时间和查询数，供下次计算 QPS
    last_report_ = now;
    last_total_ = total_queries_.load();
}
