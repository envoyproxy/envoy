#pragma once

#include <memory>

#include "envoy/config/config_provider.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

/**
 * Scope key fragment base class.
 */
class ScopeKeyFragmentBase {
public:
  bool operator!=(const ScopeKeyFragmentBase& other) const { return !(*this == other); }

  bool operator==(const ScopeKeyFragmentBase& other) const {
    if (typeid(*this) == typeid(other)) {
      return hash() == other.hash();
    }
    return false;
  }
  virtual ~ScopeKeyFragmentBase() = default;

  // Hash of the fragment.
  virtual uint64_t hash() const PURE;
};

/**
 *  Scope Key is composed of non-null fragments.
 **/
class ScopeKey {
public:
  ScopeKey() = default;
  ScopeKey(ScopeKey&& other) = default;

  // Scopekey is not copy-assignable and copy-constructible as it contains unique_ptr inside itself.
  ScopeKey(const ScopeKey&) = delete;
  ScopeKey operator=(const ScopeKey&) = delete;

  // Caller should guarantee the fragment is not nullptr.
  void addFragment(std::unique_ptr<ScopeKeyFragmentBase>&& fragment) {
    ASSERT(fragment != nullptr, "null fragment not allowed in ScopeKey.");
    updateHash(*fragment);
    fragments_.emplace_back(std::move(fragment));
  }

  uint64_t hash() const { return hash_; }
  bool operator!=(const ScopeKey& other) const;
  bool operator==(const ScopeKey& other) const;

private:
  // Update the key's hash with the new fragment hash.
  void updateHash(const ScopeKeyFragmentBase& fragment) {
    std::stringbuf buffer;
    buffer.sputn(reinterpret_cast<const char*>(&hash_), sizeof(hash_));
    const auto& fragment_hash = fragment.hash();
    buffer.sputn(reinterpret_cast<const char*>(&fragment_hash), sizeof(fragment_hash));
    hash_ = HashUtil::xxHash64(buffer.str());
  }

  uint64_t hash_{0};
  std::vector<std::unique_ptr<ScopeKeyFragmentBase>> fragments_;
};

using ScopeKeyPtr = std::unique_ptr<ScopeKey>;

// String fragment.
class StringKeyFragment : public ScopeKeyFragmentBase {
public:
  explicit StringKeyFragment(absl::string_view value)
      : value_(value), hash_(HashUtil::xxHash64(value_)) {}

  uint64_t hash() const override { return hash_; }

private:
  const std::string value_;
  const uint64_t hash_;
};

/**
 * The scoped key builder.
 */
class ScopeKeyBuilder {
public:
  virtual ~ScopeKeyBuilder() = default;

  /**
   * Based on the incoming HTTP request headers, returns the hash value of its scope key.
   * @param headers the request headers to match the scoped routing configuration against.
   * @return unique_ptr of the scope key computed from header.
   */
  virtual ScopeKeyPtr computeScopeKey(const Http::HeaderMap&) const PURE;
};

/**
 * The scoped routing configuration.
 */
class ScopedConfig : public Envoy::Config::ConfigProvider::Config {
public:
  ~ScopedConfig() override = default;

  /**
   * Based on the scope key, returns the configuration to use for selecting a target route.
   * The scope key can be got via ScopeKeyBuilder.
   *
   * @param scope_key the scope key. null config will be returned when null.
   * @return ConfigConstSharedPtr the router's Config matching the request headers.
   */
  virtual ConfigConstSharedPtr getRouteConfig(const ScopeKeyPtr& scope_key) const PURE;
};

using ScopedConfigConstSharedPtr = std::shared_ptr<const ScopedConfig>;
using ScopeKeyBuilderPtr = std::unique_ptr<const ScopeKeyBuilder>;

} // namespace Router
} // namespace Envoy
