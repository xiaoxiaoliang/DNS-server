/**
 * @file config_loader.h
 * @brief INI 风格配置文件加载器
 *
 * 解析 key=value 格式的配置文件，支持：
 *   - 注释行: 以 '#' 或 ';' 开头的行为注释
 *   - 空行自动忽略
 *   - 自动去除键值两侧的空白字符
 *   - 提供字符串和整数两种取值接口
 *
 * 配置文件示例 (dns.conf):
 *   # DNS 服务器主配置
 *   listen_address = 0.0.0.0
 *   listen_port = 53
 *   upstream_servers = 8.8.8.8:53,114.114.114.114:53
 */
#pragma once
#include <string>
#include <unordered_map>

class ConfigLoader {
public:
    /** 从指定路径加载配置文件，成功返回 true */
    bool load(const std::string& file_path);

    /** 读取字符串值，key 不存在时返回默认值 def */
    std::string get(const std::string& key, const std::string& def = "") const;

    /** 读取整数值，转换失败或 key 不存在时返回默认值 def */
    int get_int(const std::string& key, int def = 0) const;

private:
    /** 去除字符串首尾空白字符 (空格/制表符/回车/换行) */
    static std::string trim(const std::string& s);

    std::unordered_map<std::string, std::string> kv_;   // 键值对存储
};
