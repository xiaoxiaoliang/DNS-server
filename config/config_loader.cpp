/**
 * @file config_loader.cpp
 * @brief 配置文件加载器实现 —— 逐行解析 key=value
 */
#include "config_loader.h"
#include <fstream>
#include <algorithm>

bool ConfigLoader::load(const std::string& file_path) {
    std::ifstream f(file_path);
    if (!f.is_open()) return false;             // 文件打不开，直接失败

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);                      // 去除首尾空白

        // 跳过空行和注释行（# 或 ; 开头）
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        auto eq = line.find('=');               // 查找 "=" 分隔符
        if (eq == std::string::npos) continue;  // 无等号则跳过

        std::string key = trim(line.substr(0, eq));       // "=" 左边为 key
        std::string val = trim(line.substr(eq + 1));      // "=" 右边为 value
        kv_[key] = val;                         // 存入 map
    }
    return true;
}

std::string ConfigLoader::get(const std::string& key, const std::string& def) const {
    auto it = kv_.find(key);
    return it != kv_.end() ? it->second : def;  // 找不到返回默认值
}

int ConfigLoader::get_int(const std::string& key, int def) const {
    auto s = get(key);
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }   // 转换失败也返回默认值
}

std::string ConfigLoader::trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";   // 全空白字符串返回空
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}
