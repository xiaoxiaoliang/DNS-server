/**
 * @file logger.cpp
 * @brief 日志系统实现 —— 包含格式化、文件输出等核心逻辑
 */

#include "logger.h"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>

// ============================================================================
// 单例获取 —— C++11 保证静态局部变量的线程安全初始化
// ============================================================================
Logger& Logger::instance() { static Logger l; return l; }

// ============================================================================
// 初始化 —— 设置日志等级和输出文件
// 若 file_path 为空，日志仅输出到控制台（std::cout）
// 若 file_path 非空，以追加模式打开文件
// ============================================================================
void Logger::init(LogLevel level, const std::string& file_path) {
    level_ = level;
    if (!file_path.empty()) {
        file_.open(file_path, std::ios::app);
    }
}

// ============================================================================
// 核心日志输出 —— 线程安全，低于设定等级的日志被静默丢弃
// ============================================================================
void Logger::log(LogLevel lv, const std::string& msg) {
    if (lv < level_) return;                            // 级别过滤: 只输出 >= level_ 的日志
    std::lock_guard<std::mutex> lock(mtx_);             // 互斥锁保护写入
    auto line = format(lv, msg);
    if (file_.is_open()) file_ << line << std::endl;    // 写入文件（带换行和刷新）
    std::cout << line << std::endl;                     // 同时输出到控制台
}

// 各级别便捷封装
void Logger::debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
void Logger::info(const std::string& msg)  { log(LogLevel::INFO, msg); }
void Logger::warn(const std::string& msg)  { log(LogLevel::WARN, msg); }
void Logger::error(const std::string& msg) { log(LogLevel::ERROR, msg); }

// ============================================================================
// 格式化日志行 —— 格式: YYYY-MM-DD HH:MM:SS.mmm [LEVEL] message
// 例如: 2026-05-20 17:30:15.042 [INFO ] 服务器启动
// ============================================================================
std::string Logger::format(LogLevel lv, const std::string& msg) {
    auto now = std::chrono::system_clock::now();              // 获取当前时间点
    auto t = std::chrono::system_clock::to_time_t(now);       // 转为 time_t (秒精度)
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;                       // 提取毫秒部分

    // 线程安全的 localtime: Windows 用 localtime_s, Linux 用 localtime_r
    std::tm tm_buf;
    std::memset(&tm_buf, 0, sizeof(tm_buf));
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    std::ostringstream ss;
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    ss << time_buf                                                    // 日期时间
       << '.' << std::setw(3) << std::setfill('0') << ms.count()   // 毫米 (3位补零)
       << " [" << level_str(lv) << "] " << msg;                    // 级别 + 消息体
    return ss.str();
}

// ============================================================================
// 日志级别转字符串 —— 5字符宽右对齐，日志文件阅读时更清晰
// ============================================================================
const char* Logger::level_str(LogLevel lv) {
    switch (lv) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "???";
}
