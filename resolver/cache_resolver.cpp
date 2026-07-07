/**
 * @file cache_resolver.cpp
 * @brief LRU + TTL 缓存的实现
 *
 * LRU 策略的核心操作 —— splice:
 *   std::list::splice 可以在 O(1) 时间内将一个节点从一个位置移动到另一个位置，
 *   无需重新分配内存，仅修改链表指针。因此:
 *   - get() 命中后将节点 splice 到链表头部: O(1)
 *   - put() 更新时将节点 splice 到头部: O(1)
 *   - 淘汰最旧条目: 取链表尾部并 pop_back: O(1)
 *
 * TTL 过期判断使用 std::chrono::steady_clock（单调时钟），
 * 不受系统时间调整影响。expires_at 存储绝对时间戳，
 * 比较时直接与当前时间对比。
 */
#include "cache_resolver.h"
#include <chrono>

CacheResolver::CacheResolver(size_t max_entries) : max_entries_(max_entries) {}

// ============================================================================
// 缓存查询
// ============================================================================
optional<std::vector<uint8_t>> CacheResolver::get(const std::string& domain, uint16_t qtype) {
    std::lock_guard<std::mutex> lock(mtx_);                    // LRU 链表会被修改
    auto key = make_key(domain, qtype);
    auto it = map_.find(key);
    if (it == map_.end()) return nullopt;       // 未命中

    auto& entry = it->second;   // entry 是 list<shared_ptr<CacheEntry>>::iterator

    // 检查 TTL 是否过期
    auto now = std::chrono::steady_clock::now();
    if (now > (*entry)->expires_at) {
        // 过期: 从 LRU 链表和 map 中彻底删除
        lru_.erase(entry);
        map_.erase(it);
        return nullopt;
    }

    // 命中: 将条目移到 LRU 链表头部（标记为最近使用）
    lru_.splice(lru_.begin(), lru_, entry);
    return (*entry)->data;
}

// ============================================================================
// 缓存写入
// ============================================================================
void CacheResolver::put(const std::string& domain, uint16_t qtype,
                         const std::vector<uint8_t>& data, uint32_t ttl) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto key = make_key(domain, qtype);

    // 若 key 已存在，则更新数据并移动到头
    auto it = map_.find(key);
    if (it != map_.end()) {
        auto& entry = it->second;   // entry 是 list<shared_ptr<CacheEntry>>::iterator
        (*entry)->data = data;
        (*entry)->expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(ttl);
        lru_.splice(lru_.begin(), lru_, entry);
        return;
    }

    // 缓存已满: 淘汰最旧的条目（LRU 链表尾部 = 最久未访问）
    if (lru_.size() >= max_entries_) {
        auto& oldest = lru_.back();             // oldest 是 shared_ptr<CacheEntry>&
        map_.erase(make_key(oldest->domain, oldest->qtype)); // 从 map 中删除
        lru_.pop_back();                                    // 从链表中删除
    }

    // 创建新条目并插入 LRU 链表头部
    auto entry = std::make_shared<CacheEntry>();
    entry->data = data;
    entry->domain = domain;
    entry->qtype = qtype;
    entry->expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(ttl);

    lru_.push_front(entry);
    map_[key] = lru_.begin();  // 记录迭代器位置
}

// ============================================================================
// 批量过期清理 —— 遍历 LRU 链表，删除所有过期条目
// ============================================================================
size_t CacheResolver::evict_expired() {
    std::lock_guard<std::mutex> lock(mtx_);
    size_t count = 0;
    auto now = std::chrono::steady_clock::now();

    auto it = lru_.begin();
    while (it != lru_.end()) {
        if (now > (*it)->expires_at) {
            map_.erase(make_key((*it)->domain, (*it)->qtype));
            it = lru_.erase(it);  // erase 返回下一个有效迭代器
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}

std::string CacheResolver::make_key(const std::string& domain, uint16_t qtype) {
    return domain + ":" + std::to_string(qtype);
}
