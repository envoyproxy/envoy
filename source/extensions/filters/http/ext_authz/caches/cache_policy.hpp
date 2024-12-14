/**
 * \file
 * \brief Cache policy interface declaration
 */
#ifndef CACHE_POLICY_HPP
#define CACHE_POLICY_HPP

#include <unordered_set>

namespace caches
{

/**
 * \brief Cache policy abstract base class
 * \tparam Key Type of a key a policy works with
 */
template <typename Key>
class ICachePolicy
{
  public:
    virtual ~ICachePolicy() = default;

    /**
     * \brief Handle element insertion in a cache
     * \param[in] key Key that should be used by the policy
     */
    virtual void Insert(const Key &key) = 0;

    /**
     * \brief Handle request to the key-element in a cache
     * \param key
     */
    virtual void Touch(const Key &key) = 0;
    /**
     * \brief Handle element deletion from a cache
     * \param[in] key Key that should be used by the policy
     */
    virtual void Erase(const Key &key) = 0;

    /**
     * \brief Return a key of a replacement candidate
     * \return Replacement candidate according to selected policy
     */
    virtual const Key &ReplCandidate() const = 0;
};

/**
 * \brief Basic no caching policy class
 * \details Preserve any key provided. Erase procedure can get rid of any added keys
 * without specific rules: a replacement candidate will be the first element in the
 * underlying container. As unordered container can be used in the implementation
 * there are no warranties that the first/last added key will be erased
 * \tparam Key Type of a key a policy works with
 */
template <typename Key>
class NoCachePolicy : public ICachePolicy<Key>
{
  public:
    NoCachePolicy() = default;
    ~NoCachePolicy() noexcept override = default;

    void Insert(const Key &key) override
    {
        key_storage.emplace(key);
    }

    void Touch(const Key &key) noexcept override
    {
        // do not do anything
        (void)key;
    }

    void Erase(const Key &key) noexcept override
    {
        key_storage.erase(key);
    }

    // return a key of a displacement candidate
    const Key &ReplCandidate() const noexcept override
    {
        return *key_storage.cbegin();
    }

  private:
    std::unordered_set<Key> key_storage;
};
} // namespace caches

#endif // CACHE_POLICY_HPP
