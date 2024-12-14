/**
 * \file
 * \brief LRU cache implementation
 */
#ifndef LRU_CACHE_POLICY_HPP
#define LRU_CACHE_POLICY_HPP

#include "cache.hpp"
#include "cache_policy.hpp"
#include <list>
#include <unordered_map>

namespace caches
{
/**
 * \brief LRU (Least Recently Used) cache policy
 * \details LRU policy in the case of replacement removes the least recently used element.
 * That is, in the case of replacement necessity, that cache policy returns a key that
 * has not been touched recently. For example, cache maximum size is 3 and 3 elements have
 * been added - `A`, `B`, `C`. Then the following actions were made:
 * ```
 * Cache placement order: A, B, C
 * Cache elements: A, B, C
 * # Cache access:
 * - A touched, B touched
 * # LRU element in the cache: C
 * # Cache access:
 * - B touched, C touched
 * # LRU element in the cache: A
 * # Put new element: D
 * # LRU replacement candidate: A
 *
 * Cache elements: B, C, D
 * ```
 * \tparam Key Type of a key a policy works with
 */
template <typename Key>
class LRUCachePolicy : public ICachePolicy<Key>
{
  public:
    using lru_iterator = typename std::list<Key>::iterator;

    LRUCachePolicy() = default;
    ~LRUCachePolicy() = default;

    void Insert(const Key &key) override
    {
        lru_queue.emplace_front(key);
        key_finder[key] = lru_queue.begin();
    }

    void Touch(const Key &key) override
    {
        // move the touched element at the beginning of the lru_queue
        lru_queue.splice(lru_queue.begin(), lru_queue, key_finder[key]);
    }

    void Erase(const Key &) noexcept override
    {
        // remove the least recently used element
        key_finder.erase(lru_queue.back());
        lru_queue.pop_back();
    }

    // return a key of a displacement candidate
    const Key &ReplCandidate() const noexcept override
    {
        return lru_queue.back();
    }

  private:
    std::list<Key> lru_queue;
    std::unordered_map<Key, lru_iterator> key_finder;
};
} // namespace caches

#endif // LRU_CACHE_POLICY_HPP
