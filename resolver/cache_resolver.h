/**
 * @file cache_resolver.h
 * @brief DNS 缓存 —— 基于 LRU + TTL 双过期策略的 DNS 响应缓存
 *
 * 设计要点:
 *   - LRU (Least Recently Used): 最近最久未使用淘汰策略
 *     缓存满时，淘汰最久未被访问的条目
 *   - TTL (Time To Live): 生存时间过期
 *     每个条目有独立的过期时间戳，过期后自动失效
 *   - std::shared_mutex: 读共享/写独占锁
 *     get() 使用 shared_lock（多条线程可同时读取）
 *     put()/evict_expired() 使用 unique_lock（独占写入）
 *   - splice 优化: 命中时用 list::splice 将条目移到链表头部，O(1) 复杂度
 *
 * 数据结构:
 *   lru_ = list<shared_ptr<CacheEntry>>  (LRU 链表，头部=最近访问)
 *   map_ = map<key, list::iterator>      (O(1) 查找，iterator 直接定位链表位置)
 */
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <memory>
#include <mutex>
#include <chrono>
#include "../common/optional.h"
#include <cstdint>

class CacheResolver {
public:
    /** 缓存条目 —— 存储完整的 DNS 响应报文和元数据 */
    struct CacheEntry {
        std::vector<uint8_t> data;         // 原始 DNS 响应报文
        std::string domain;                // 查询域名
        uint16_t qtype;                    // 查询类型 (A=1, AAAA=28)
        std::chrono::steady_clock::time_point expires_at; // 过期时间戳
    };

    /**
     * 构造函数
     * @param max_entries 最大缓存条目数（默认 10000），超出后触发 LRU 淘汰
     */
    explicit CacheResolver(size_t max_entries = 10000);
    ~CacheResolver() = default;

    /**
     * 从缓存获取 DNS 响应
     * @return 找到且未过期 → 返回原始响应报文; 未找到/已过期 → nullopt
     *         命中时自动将条目移到 LRU 链表头部（标记为最新使用）
     */
    optional<std::vector<uint8_t>> get(const std::string& domain, uint16_t qtype);

    /**
     * 将 DNS 响应存入缓存
     * 若 key 已存在则更新，若缓存满则淘汰最旧的条目
     */
    void put(const std::string& domain, uint16_t qtype, const std::vector<uint8_t>& data, uint32_t ttl);

    /** 清理所有已过期的缓存条目，返回清理数量 */
    size_t evict_expired();

private:
    /** 生成缓存键: "域名:查询类型"，例如 "example.com:1" */
    static std::string make_key(const std::string& domain, uint16_t qtype);

    size_t max_entries_;
    // LRU 链表: 头部 = 最近访问，尾部 = 最久未访问（将被淘汰）
    std::list<std::shared_ptr<CacheEntry> > lru_;
    // 哈希表: key → LRU 链表迭代器，实现 O(1) 查找和 O(1) 删除
    std::unordered_map<std::string, std::list<std::shared_ptr<CacheEntry> >::iterator> map_;
    mutable std::mutex mtx_;  // 互斥锁
};
