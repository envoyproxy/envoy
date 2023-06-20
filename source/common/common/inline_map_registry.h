#pragma once

#include <cstddef>
#include <cstdint>

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {

/**
 * This class could be used as an alternative of normal hash map to store the key/value pairs.
 * But by this class, you can register some frequently used keys as inline keys and get the
 * handle for the key. Then you can use the handle to access the key/value pair in the map without
 * even one time hash searching.
 *
 * This is useful when some keys are frequently used and the keys are known at compile time or
 * bootstrap time. You can get superior performance by using the inline handle. For example, the
 * filter state always uses the filter name as the key and the filter name is known at compile time.
 * By using the inline handle, the filter state could get the key/value pair without any hash.
 *
 * This class also provides the interface to access the key/value pair by the normal key and the
 * interface has similar searching performance as the normal hash map. But the insertion and removal
 * by the normal key is slower than the normal hash map.
 *
 *
 * These is usage example:
 *
 *   // Define registry to store the inline keys. The registry should have lifetime that no shorter
 *   // than all inline maps created by the registry.
 *   InlineMapRegistry registry;
 *
 *   // Register the inline key. We should never do this after bootstrapping. Typically, we can
 *   // do this by define a global variable.
 *   auto inline_handle = registry.registerInlineKey("inline_key");
 *
 *   // Finalize the registry. No further changes are allowed to the registry after this point.
 *   registry.finalize();
 *
 *   // Create the inline map.
 *   auto inline_map = registry.createInlineMap<std::string>();
 *
 *   // Get value by inline handle.
 *   inline_map->insert(inline_handle, std::make_unique<std::string>("value"));
 *   EXPECT_EQ(*inline_map->lookup(inline_handle), "value");
 */
class InlineMapRegistry {
public:
  /**
   * This is the handle used to access the key/value pair in the inline map. The handle is
   * lightweight and could be copied and passed by value.
   */
  class InlineKey {
  public:
    /**
     * Get the id of the inline map registry that the handle created by. This will be used
     * to determine if the handle could be accepted by a inline map.
     */
    uint64_t registryId() const { return registry_id_; }

    /**
     * Get the id of the inline entry in the inline map. This could be used to access the
     * key/value pair in the inline map.
     */
    uint64_t inlineId() const { return inline_id_; }

    /**
     * Get the key view of the inline key.
     */
    absl::string_view inlineKey() const { return inline_key_; }

  private:
    friend class InlineMapRegistry;

    // This constructor should only be called by InlineMapRegistry.
    InlineKey(uint64_t registry_id, uint64_t inline_id, absl::string_view inline_key)
        : registry_id_(registry_id), inline_id_(inline_id), inline_key_(inline_key) {}

    uint64_t registry_id_{};
    uint64_t inline_id_{};
    absl::string_view inline_key_;
  };

  // This is the Hash used for registration map/set and underlay hash map.
  using Hash = absl::container_internal::hash_default_hash<std::string>;

  using RegistrationMap = absl::flat_hash_map<absl::string_view, uint64_t, Hash>;

  // Set to store the inline keys. Because the node of the set have stable address, we can reference
  // the key in the handle or registration map safely.
  using RegistrationSet = absl::node_hash_set<std::string, Hash>;

  template <class T> class InlineMap : public InlineStorage {
  public:
    using TPtr = std::unique_ptr<T>;
    using UnderlayHashMap = absl::flat_hash_map<std::string, TPtr, Hash>;

    // Get the entry for the given key. If the key is not found, return nullptr.
    const T* lookup(absl::string_view key) const { return lookupImpl(key); }
    T* lookup(absl::string_view key) { return lookupImpl(key); }

    // Get the entry for the given handle. If the handle is not valid, return nullptr.
    const T* lookup(InlineKey handle) const { return lookupImpl(handle); }
    T* lookup(InlineKey handle) { return lookupImpl(handle); }

    // Set the entry for the given key. If the key is already present, overwrite it.
    void insert(absl::string_view key, TPtr value) {
      // Compute the hash value for the key and avoid computing it again in the lookup.
      const size_t hash_value = absl::Hash<absl::string_view>()(key);

      if (auto entry_id = inlineLookup(key, hash_value); entry_id.has_value()) {
        resetInlineMapEntry(*entry_id, std::move(value));
      } else {
        normal_entries_[key] = std::move(value);
      }
    }

    // Set the entry for the given handle. If the handle is not valid, do nothing.
    void insert(InlineKey handle, TPtr value) {
      if (handle.registryId() != registry_.registryId()) {
        return;
      }

      ASSERT(handle.inlineId() < registry_.registrationNum());
      resetInlineMapEntry(handle.inlineId(), std::move(value));
    }

    // Remove the entry for the given key. If the key is not found, do nothing.
    void remove(absl::string_view key) {
      // Compute the hash value for the key and avoid computing it again in the lookup.
      const size_t hash_value = absl::Hash<absl::string_view>()(key);

      if (auto entry_id = inlineLookup(key, hash_value); entry_id.has_value()) {
        resetInlineMapEntry(*entry_id);
      } else {
        normal_entries_.erase(key);
      }
    }

    // Remove the entry for the given handle. If the handle is not valid, do nothing.
    void remove(InlineKey handle) {
      if (handle.registryId() != registry_.registryId()) {
        return;
      }
      resetInlineMapEntry(handle.inlineId());
    }

    void iterate(std::function<bool(absl::string_view, T*)> callback) const {
      for (const auto& entry : normal_entries_) {
        if (!callback(entry.first, entry.second.get())) {
          return;
        }
      }

      for (const auto& id : registry_.registrationMap()) {
        ASSERT(id.second < registry_.registrationNum());

        auto entry = inline_entries_[id.second];
        if (entry == nullptr) {
          continue;
        }

        if (!callback(id.first, entry.get())) {
          return;
        }
      }
    }

    uint64_t size() const { return normal_entries_.size() + inline_entries_size_; }

    bool empty() const { return size() == 0; }

    // Get the registry id that the inline map created by.
    uint64_t registryId() const { return registry_.registryId(); }

    ~InlineMap() {
      for (uint64_t i = 0; i < registry_.registrationNum(); ++i) {
        auto entry = inline_entries_[i];
        if (entry != nullptr) {
          delete entry;
        }
      }
      memset(inline_entries_, 0, registry_.registrationNum() * sizeof(TPtr));
    }

  private:
    friend class InlineMapRegistry;

    static std::unique_ptr<InlineMap> create(InlineMapRegistry& registry) {
      return std::unique_ptr<InlineMap<T>>(new (
          (registry.registrationMap().size() * sizeof(InlineMap<T>::TPtr))) InlineMap(registry));
    }

    InlineMap(InlineMapRegistry& registry) : registry_(registry) {
      // Initialize the inline entries to nullptr.
      memset(inline_entries_, 0, registry_.registrationNum() * sizeof(TPtr));
    }

    T* lookupImpl(absl::string_view key) const {
      // Compute the hash value for the key and avoid computing it again in the lookup.
      const size_t hash_value = InlineMapRegistry::Hash()(key);

      // Because the normal string view key is used here, try the normal map entry first.
      if (auto it = normal_entries_.find(key, hash_value); it != normal_entries_.end()) {
        return it->second.get();
      }

      if (auto entry_id = inlineLookup(key, hash_value); entry_id.has_value()) {
        return inline_entries_[*entry_id];
      }

      return nullptr;
    }

    T* lookupImpl(InlineKey handle) const {
      if (handle.registryId() != registry_.registryId()) {
        return nullptr;
      }

      ASSERT(handle.inlineId() < registry_.registrationNum());
      return inline_entries_[handle.inlineId()];
    }

    void resetInlineMapEntry(uint64_t inline_entry_id, TPtr new_entry = nullptr) {
      ASSERT(inline_entry_id < registry_.registrationNum());
      if (auto entry = inline_entries_[inline_entry_id]; entry != nullptr) {
        // Remove and delete the old valid entry.
        ASSERT(inline_entries_size_ > 0);
        --inline_entries_size_;
        delete entry;
      }

      if (new_entry != nullptr) {
        // Append the new valid entry.
        ++inline_entries_size_;
      }

      inline_entries_[inline_entry_id] = new_entry.release();
    }

    absl::optional<uint64_t> inlineLookup(absl::string_view key, size_t hash_value) const {
      const auto& map_ref = registry_.registrationMap();
      if (auto iter = map_ref.find(key, hash_value); iter != map_ref.end()) {
        return iter->second;
      }
      return absl::nullopt;
    }

    // This is the reference of the registry that the inline map created by. This is used to
    // validate the inline handle validity and get the inline key set.
    const InlineMapRegistry& registry_;

    // This is the underlay hash map for the normal entries.
    UnderlayHashMap normal_entries_;

    uint64_t inline_entries_size_{};
    // This should be the last member of the class and no member should be added after this.
    T* inline_entries_[];
  };

  template <class T> using InlineMapPtr = std::unique_ptr<InlineMap<T>>;

  InlineMapRegistry() : registry_id_(generateRegistryId()) {}

  /**
   * Register a custom inline key. May only be called before finalized().
   */
  InlineKey registerInlineKey(absl::string_view key);

  /**
   * Fetch the inline handle for the given key. May only be called after finalized(). This should
   * be used to get the inline handle for the key registered by registerInlineKey(). This function
   * could used to determine if the key is registered or not at runtime or xDS config loading time
   * and decide if the key should be used as inline key or normal key.
   */
  absl::optional<InlineKey> getInlineKey(absl::string_view key) const;

  /**
   * Get the registration map that contains all registered inline keys. May only be called after
   * finalized().
   */
  const RegistrationMap& registrationMap() const;

  /**
   * Get the registration set that contains all registered inline keys. May only be called after
   * finalized().
   */
  const RegistrationSet& registrationSet() const { return registration_set_; }

  /**
   * Get the number of the registrations. May only be called after finalized().
   */
  uint64_t registrationNum() const;

  uint64_t registryId() const { return registry_id_; }

  // Finalize this registry. No further changes are allowed after this point. This guaranteed that
  // all map created by the process have the same variable size and custom registrations. This
  // function could be called multiple times safely and only the first call will have effect.
  void finalize() { finalized_ = true; }

  template <class T> InlineMapPtr<T> createInlineMap() {
    ASSERT(finalized_, "Cannot create inline map before finalized()");

    // Call finalize() anyway to make sure that the registry is finalized and no any new inline
    // keys could be registered.
    finalize();
    return InlineMap<T>::create(*this);
  }

private:
  static std::atomic<uint64_t> global_registry_id_;
  static uint64_t generateRegistryId() { return global_registry_id_++; }

  const uint64_t registry_id_{};

  bool finalized_{};
  RegistrationSet registration_set_;
  RegistrationMap registration_map_;
};

/**
 * Helper class to get inline map registry singleton based on the scope. If cross-modules inline map
 * registry is necessary, this class should be used.
 */
class InlineMapRegistryHelper {
public:
  using ManagedRegistries = absl::flat_hash_map<std::string, InlineMapRegistry*>;

  /**
   * Get the inline map registry singleton based on the scope.
   */
  template <class Scope>
  static InlineMapRegistry::InlineKey registerInlinKey(absl::string_view key) {
    return mutableRegistry<Scope>().registerInlineKey(key);
  }

  /**
   * Get registered inline key by the key name.
   */
  template <class Scope>
  static absl::optional<InlineMapRegistry::InlineKey> getInlineKey(absl::string_view key) {
    return mutableRegistry<Scope>().getInlineKey(key);
  }

  /**
   * Create inline map based on the scope.
   */
  template <class Scope, class T> static InlineMapRegistry::InlineMapPtr<T> createInlineMap() {
    return mutableRegistry<Scope>().template createInlineMap<T>();
  }

  /**
   * Get all registries managed by this helper. This this helpful to finalize all registries and
   * print the debug info of inline map registry.
   */
  static const ManagedRegistries& registries() { return mutableRegistries(); }

  /**
   * Finalize all registries managed by this helper. This should be called after all inline keys
   * are registered.
   */
  static void finalize() {
    for (auto& registry : mutableRegistries()) {
      registry.second->finalize();
    }
  }

private:
  template <class Scope> static InlineMapRegistry& mutableRegistry() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(InlineMapRegistry, []() {
      auto* registry = new InlineMapRegistry();
      auto result = InlineMapRegistryHelper::mutableRegistries().emplace(Scope::name(), registry);
      RELEASE_ASSERT(result.second, "Duplicate inline map registry for scope: " + Scope::name());
      return registry;
    }());
  }

  static ManagedRegistries& mutableRegistries() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(ManagedRegistries);
  }
};

} // namespace Envoy
