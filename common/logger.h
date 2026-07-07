/**
 * @file logger.h
 * @brief 线程安全的日志系统 —— 支持控制台和文件双输出
 *
 * 设计要点：
 *   - 单例模式（通过 instance() 访问全局唯一实例）
 *   - 线程安全 —— 内部使用 std::mutex 保护写操作
 *   - 分级输出 —— DEBUG/INFO/WARN/ERROR 四级，低于设定级别的日志会被过滤
 *   - 时间戳精度 —— 毫秒级时间戳，格式为: YYYY-MM-DD HH:MM:SS.mmm
 *   - 双通道 —— 若指定了日志文件则写入文件，否则输出到控制台
 *
 * 使用方式:
 *   Logger::instance().init(LogLevel::DEBUG, "dns.log");
 *   LOG_INFO("服务器启动");
 *   LOG_ERROR("转发失败: " + reason);
 */
#pragma once
#include <string>
#include <mutex>
#include <fstream>
#include <chrono>
#include <sstream>

// ============================================================================
// 日志等级 —— 数值越大越重要
// 注: #undef ERROR 防止 <windows.h> 的 ERROR 宏污染
// ============================================================================
#ifdef ERROR
#undef ERROR
#endif
enum class LogLevel {
    DEBUG = 0,   // 调试信息 —— 详细日志，用于开发和排错
    INFO  = 1,   // 一般信息 —— 正常运行中的关键节点记录
    WARN  = 2,   // 警告 —— 可恢复的异常情况
    ERROR = 3    // 错误 —— 需要关注的故障信息
};

class Logger {
public:
    /** 获取单例实例（线程安全的懒汉式初始化） */
    static Logger& instance();

    /** 初始化日志系统 —— 设置最低日志级别和日志文件路径 */
    void init(LogLevel level, const std::string& file_path);

    /** 底层日志输出 —— 由各等级便捷方法调用 */
    void log(LogLevel lv, const std::string& msg);

    // 便捷方法 —— 分别输出不同等级的日志
    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

private:
    Logger() = default;       // 构造函数私有 —— 强制通过 instance() 获取单例

    /** 格式化日志行 —— 生成带时间戳和级别的完整日志字符串 */
    std::string format(LogLevel lv, const std::string& msg);

    /** 将日志等级枚举转为 5 字符对齐的字符串 */
    static const char* level_str(LogLevel lv);

    LogLevel level_ = LogLevel::INFO;   // 默认日志级别: INFO
    std::mutex mtx_;                    // 互斥锁 —— 保证多线程写入安全
    std::ofstream file_;               // 日志文件流 —— 为空则不写文件
};

// ============================================================================
// 便捷宏 —— 简化日志调用，无需每次写 Logger::instance()
// ============================================================================
#define LOG_DEBUG(msg) Logger::instance().debug(msg)
#define LOG_INFO(msg)  Logger::instance().info(msg)
#define LOG_WARN(msg)  Logger::instance().warn(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)
