#pragma once

#include <cstddef>
#include <cstdint>

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {

class UntypedInlineHandle {
public:
  UntypedInlineHandle(uint64_t inline_scope_id, uint64_t inline_entry_id,
                      absl::string_view inline_entry_key)
      : inline_scope_id_(inline_scope_id), inline_entry_id_(inline_entry_id),
        inline_entry_key_(inline_entry_key) {}

  uint64_t inlineScopeId() const { return inline_scope_id_; }

  uint64_t inlineEntryId() const { return inline_entry_id_; }

  absl::string_view inlineEntryKey() const { return inline_entry_key_; }

private:
  uint64_t inline_scope_id_{};
  uint64_t inline_entry_id_{};
  absl::string_view inline_entry_key_;
};

/**
 * Information about the registry. This is used to print out the registry debug information.
 */
struct InlineMapRegistryDebugInfo {
  // Registry scope id for the registry, this is generated dynamically by the
  // InlineMapRegistryManager.
  uint64_t registry_scope_id;
  // Registry scope name for the registry, typically short string from
  // scope type.
  absl::string_view registry_scope_name;
  // All inline keys registered for the registry.
  std::vector<absl::string_view> registry_inline_keys;
};

class InlineMapRegistryManager {
public:
  static const std::vector<InlineMapRegistryDebugInfo>& registryInfos() {
    // Call finalize() to ensure that all finalizers are called.
    finalize();
    return mutableRegistryInfos();
  }

  /**
   * Generate a scope id for the given scope and register a finalizer to be called when
   * InlineMapRegistry::finalize() is called.
   *
   * NOTE: This is called by InlineMapRegistry::scopeId() and should never be called
   * manually.
   */
  template <class Scope> static uint64_t generateScopeId();

private:
  using Finalizer = std::function<InlineMapRegistryDebugInfo()>;

  static uint64_t nextScopeId() {
    static uint64_t next_scope_id = 0;
    return next_scope_id++;
  }

  /**
   * Call all finalizers. This should only be called once, after all registrations are
   * complete.
   */
  static void finalize() {
    while (!mutableFinalizers().empty()) {
      mutableRegistryInfos().push_back(mutableFinalizers().back()());
      mutableFinalizers().pop_back();
    }
  }

  static std::vector<InlineMapRegistryDebugInfo>& mutableRegistryInfos() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(std::vector<InlineMapRegistryDebugInfo>);
  }

  static std::vector<Finalizer>& mutableFinalizers() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(std::vector<Finalizer>);
  }
};

/**
 * This template could be used as an alternative of normal hash map to store the key/value pairs.
 * But by this template, you can register some frequently used keys as inline keys and get the
 * handle for the key. Then you can use the handle to access the key/value pair in the map without
 * even one time hash searching.
 *
 * This is useful when some keys are frequently used and the keys are known at compile time or
 * bootstrap time. You can get superior performance by using the inline handle. For example, the
 * filter state uses always use the filter name as the key and the filter name is known at compile
 * time. By using the inline handle, the filter state could get the key/value pair without any hash.
 *
 * This template also provides the interface to access the key/value pair by the normal key and the
 * interface has similar searching performance as the normal hash map. But the insertion and removal
 * by the normal key is slower than the normal hash map.
 *
 * The Scope template parameter is used to distinguish different inline map registry. Different
 * Scope will have different inline key registrations and different scope id.
 *
 * These is usage example:
 *
 *   // Define a scope type.
 *   class ExampleScope {
 *   public:
 *     static absl::string_view name() { return "ExampleScope"; }
 *   };
 *
 *   // Register the inline key. We should never do this after bootstrapping.
 *   static auto inline_handle = InlineMapRegistry<ExampleScope>::registerInlineKey("inline_key");
 *
 *   // Create the inline map.
 *   auto inline_map = InlineMapRegistry<ExampleScope>::InlineMap<std::string>::createInlineMap();
 *
 *   // Get value by inline handle.
 *   inline_map->insert(inline_handle, std::make_unique<std::string>("value"));
 *   EXPECT_EQ(*inline_map->lookup(inline_handle), "value");
 */
template <class Scope> class InlineMapRegistry {
public:
  class InlineHandle {
  public:
    uint64_t inlineEntryId() const { return inline_entry_id_; }

    absl::string_view inlineEntryKey() const { return inline_entry_key_; }

  private:
    friend class InlineMapRegistry;

    // This constructor should only be called by InlineMapRegistry.
    InlineHandle(uint64_t inline_entry_id, absl::string_view inline_entry_key)
        : inline_entry_id_(inline_entry_id), inline_entry_key_(inline_entry_key) {}

    uint64_t inline_entry_id_{};
    absl::string_view inline_entry_key_;
  };

  // This is the Hash used for registration map/set and underlay hash map.
  using Hash = absl::container_internal::hash_default_hash<std::string>;

  using RegistrationMap = absl::flat_hash_map<absl::string_view, uint64_t, Hash>;

  // Node hash set is used to store the registration keys because it's entries have stable address
  // and it is safe to reference the key in the InlineHandle or RegistrationMap.
  using RegistrationSet = absl::node_hash_set<std::string, Hash>;

  template <class T> class InlineMap : public InlineStorage {
  public:
    using TPtr = std::unique_ptr<T>;
    using RawT = T*;
    using ConstRawT = const T*;
    using UnderlayHashMap = absl::flat_hash_map<std::string, TPtr, Hash>;

    // Get the entry for the given key. If the key is not found, return nullptr.
    ConstRawT lookup(absl::string_view key) const { return lookupImpl(key); }
    RawT lookup(absl::string_view key) { return lookupImpl(key); }

    // Get the entry for the given handle. If the handle is not valid, return nullptr.
    ConstRawT lookup(InlineHandle handle) const { return lookupImpl(handle); }
    RawT lookup(InlineHandle handle) { return lookupImpl(handle); }

    // Get the entry for the given untyped handle. If the handle is not valid, return nullptr.
    ConstRawT lookup(UntypedInlineHandle handle) const { return lookupImpl(handle); }
    RawT lookup(UntypedInlineHandle handle) { return lookupImpl(handle); }

    // Set the entry for the given key. If the key is already present, overwrite it.
    void insert(absl::string_view key, TPtr value) {
      // Compute the hash value for the key and avoid computing it again in the lookup.
      const size_t hash_value = absl::Hash<absl::string_view>()(key);

      if (auto entry_id = staticLookup(key, hash_value); entry_id.has_value()) {
        resetInlineMapEntry(*entry_id, std::move(value));
      } else {
        normal_entries_[key] = std::move(value);
      }
    }

    // Set the entry for the given handle. If the handle is not valid, do nothing.
    void insert(InlineHandle handle, TPtr value) {
      ASSERT(handle.inlineEntryId() < InlineMapRegistry::registrationMapSize());
      resetInlineMapEntry(handle.inlineEntryId(), std::move(value));
    }

    // Set the entry for the given untyped handle. If the handle is not valid, do nothing.
    void insert(UntypedInlineHandle handle, TPtr value) {
      // If the scope id does not match, the handle is not valid.
      if (handle.inlineScopeId() != InlineMapRegistry::scopeId()) {
        return;
      }

      // If the entry id is valid, it is an inline entry.
      if (handle.inlineEntryId() < InlineMapRegistry::registrationMapSize()) {
        resetInlineMapEntry(handle.inlineEntryId(), std::move(value));
        return;
      }

      // Otherwise, try normal map entry.
      normal_entries_[handle.inlineEntryKey()] = std::move(value);
    }

    // Remove the entry for the given key. If the key is not found, do nothing.
    void remove(absl::string_view key) {
      // Compute the hash value for the key and avoid computing it again in the lookup.
      const size_t hash_value = absl::Hash<absl::string_view>()(key);

      if (auto entry_id = staticLookup(key, hash_value); entry_id.has_value()) {
        resetInlineMapEntry(*entry_id);
      } else {
        normal_entries_.erase(key);
      }
    }

    // Remove the entry for the given handle. If the handle is not valid, do nothing.
    void remove(InlineHandle handle) { resetInlineMapEntry(handle.inlineEntryId()); }

    // Remove the entry for the given untyped handle. If the handle is not valid, do nothing.
    void remove(UntypedInlineHandle handle) {
      // If the scope id does not match, the handle is not valid.
      if (handle.inlineScopeId() != InlineMapRegistry::scopeId()) {
        return;
      }

      // If the entry id is valid, it is an inline entry.
      if (handle.inlineEntryId() < InlineMapRegistry::registrationMapSize()) {
        resetInlineMapEntry(handle.inlineEntryId());
        return;
      }

      // Otherwise, try normal map entry.
      normal_entries_.erase(handle.inlineEntryKey());
    }

    void iterate(std::function<bool(absl::string_view, RawT)> callback) const {
      for (const auto& entry : normal_entries_) {
        if (!callback(entry.first, entry.second.get())) {
          return;
        }
      }

      for (const auto& id : InlineMapRegistry::registrationMap()) {
        ASSERT(id.second < InlineMapRegistry::registrationMapSize());

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

    static std::unique_ptr<InlineMap> createInlineMap() {
      return std::unique_ptr<InlineMap>(
          new ((InlineMapRegistry::registrationMapSize() * sizeof(TPtr))) InlineMap());
    }

  private:
    InlineMap() {
      memset(inline_entries_, 0, InlineMapRegistry::registrationMapSize() * sizeof(TPtr));
    }

    ~InlineMap() {
      for (uint64_t i = 0; i < InlineMapRegistry::registrationMapSize(); ++i) {
        auto entry = inline_entries_[i];
        if (entry != nullptr) {
          delete entry;
        }
      }
      memset(inline_entries_, 0, InlineMapRegistry::registrationMapSize() * sizeof(TPtr));
    }

    RawT lookupImpl(absl::string_view key) const {
      // Compute the hash value for the key and avoid computing it again in the lookup.
      const size_t hash_value = InlineMapRegistry::Hash()(key);

      // Because the normal string view key is used here, try the normal map entry first.
      if (auto it = normal_entries_.find(key, hash_value); it != normal_entries_.end()) {
        return it->second.get();
      }

      if (auto entry_id = staticLookup(key, hash_value); entry_id.has_value()) {
        return inline_entries_[*entry_id];
      }

      return nullptr;
    }

    RawT lookupImpl(InlineHandle handle) const {
      ASSERT(handle.inlineEntryId() < InlineMapRegistry::registrationMapSize());
      return inline_entries_[handle.inlineEntryId()];
    }

    // Get the entry for the given untyped handle. If the handle is not valid, return nullptr.
    RawT lookupImpl(UntypedInlineHandle handle) const {
      // If the scope id does not match, the handle is not valid.
      if (handle.inlineScopeId() != InlineMapRegistry::scopeId()) {
        return nullptr;
      }

      // If the entry id is valid, it is an inline entry.
      if (handle.inlineEntryId() < InlineMapRegistry::registrationMapSize()) {
        return inline_entries_[handle.inlineEntryId()];
      }

      // Otherwise, try normal map entry.
      if (auto it = normal_entries_.find(handle.inlineEntryKey()); it != normal_entries_.end()) {
        return it->second.get();
      }
      return nullptr;
    }

    void resetInlineMapEntry(uint64_t inline_entry_id, TPtr new_entry = nullptr) {
      ASSERT(inline_entry_id < InlineMapRegistry::registrationMapSize());
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

    absl::optional<uint64_t> staticLookup(absl::string_view key, size_t hash_value) const {
      if (auto iter = InlineMapRegistry::registrationMap().find(key, hash_value);
          iter != InlineMapRegistry::registrationMap().end()) {
        return iter->second;
      }
      return absl::nullopt;
    }

    // This is the underlay hash map for the normal entries.
    UnderlayHashMap normal_entries_;

    uint64_t inline_entries_size_{};
    // This should be the last member of the class and no member should be added after this.
    RawT inline_entries_[];
  };

  /**
   * Register a custom inline key. May only be called before finalized().
   */
  static InlineHandle registerInlineKey(absl::string_view key) {
    RELEASE_ASSERT(!mutableFinalized(), "Cannot register inline key after finalized()");

    // Initialize the custom inline key registrations. If this is the first time this function
    // is called, this will generate a scope id and register a finalizer to the manager.
    // Otherwise, this will be a no-op.
    initialize();

    // If the key is already registered, return the existing handle.
    if (auto it = mutableRegistrationMap().find(key); it != mutableRegistrationMap().end()) {
      // It is safe to reference the key here because the key is stored in the node hash set.
      return InlineHandle(it->second, it->first);
    }

    // If the key is not registered, create a new handle for it.

    const uint64_t entry_id = mutableRegistrationMap().size();

    // Insert the key to the node hash set and keep the reference of the key in the
    // inline handle and registration map.
    auto result = mutableRegistrationSet().emplace(key);
    RELEASE_ASSERT(result.second, "The key is already registered and this should never happen");

    // It is safe to reference the key here because the key is stored in the node hash set.
    mutableRegistrationMap().emplace(absl::string_view(*result.first), entry_id);
    return InlineHandle(entry_id, absl::string_view(*result.first));
  }

  /**
   * Fetch the inline handle for the given key. May only be called after finalized(). This should
   * be used to get the inline handle for the key registered by registerInlineKey(). This function
   * could used to determine if the key is registered or not at runtime or xDS config loading time
   * and decide if the key should be used as inline key or normal key.
   */
  absl::optional<InlineHandle> getInlineHandle(absl::string_view key) {
    ASSERT(mutableFinalized(), "Cannot get inline handle before finalized()");

    if (auto it = mutableRegistrationMap().find(key); it != mutableRegistrationMap().end()) {
      // It is safe to reference the key here because the key is stored in the node hash set.
      return InlineHandle(it->second, it->first);
    }

    return absl::nullopt;
  }

  /**
   * Fetch all registered key. May only be called after finalized().
   */
  static const RegistrationMap& registrationMap() {
    ASSERT(mutableFinalized(), "Cannot fetch registration map before finalized()");
    return mutableRegistrationMap();
  }

  /**
   * Fetch the number of registered keys. This will call finalize() automatically to ensure that
   * no further changes are allowed.
   */
  static uint64_t registrationMapSize() {
    static uint64_t size = []() {
      finalize();
      return mutableRegistrationMap().size();
    }();

    return size;
  }

  /**
   * Get the scope id for this registry. When called for the first time, this will generate a
   * id and register a finalizer to the InlineMapRegistryManager.
   */
  static uint64_t scopeId() {
    // Scope id is generated on first use. Static ensures that it is only generated once.
    static uint64_t scope_id = InlineMapRegistryManager::generateScopeId<Scope>();
    return scope_id;
  }

private:
  friend class InlineMapRegistryManager;

  /**
   * Finalize the custom inline key registrations. No further changes are allowed after this
   * point. This guaranteed that all map created by the process have the same variable size and
   * custom registrations. This function could be called multiple times safely and only the first
   * call will have effect.
   *
   * NOTE: This should only be called by InlineMapRegistryManager with the finalizer or be called
   * in the registrationMapSize.
   */
  static InlineMapRegistryDebugInfo finalize() {
    // Initialize the custom inline key registrations. If the initialize() or scopeId() is never
    // called before, this call here will generate a scope id and register a finalizer to the
    // manager to ensure that the InlineMapRegistryManager always could get all the debug info.
    // If the initialize() or scopeId() is already called before, this will be a no-op.
    initialize();

    // Mark the registry as finalized to ensure that no further changes are allowed.
    mutableFinalized() = true;
    return debugInfo();
  }

  /**
   * Initialize the custom inline key registrations. This may be called multiple times but only
   * the first call will have effect and the rest will be no-op.
   */
  static void initialize() {
    // Call scopeId() to ensure that the scope is initialized.
    scopeId();
  }

  static bool& mutableFinalized() { MUTABLE_CONSTRUCT_ON_FIRST_USE(bool); }

  static RegistrationMap& mutableRegistrationMap() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(RegistrationMap);
  }

  static RegistrationSet& mutableRegistrationSet() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(RegistrationSet);
  }

  static InlineMapRegistryDebugInfo debugInfo() {
    RELEASE_ASSERT(mutableFinalized(), "Cannot fetch debug info before finalized()");

    std::vector<absl::string_view> all_inline_keys;
    all_inline_keys.reserve(mutableRegistrationMap().size());
    for (const auto& entry : mutableRegistrationMap()) {
      all_inline_keys.push_back(absl::string_view(entry.first));
    }

    return {scopeId(), Scope::name(), std::move(all_inline_keys)};
  }
};

template <class Scope> uint64_t InlineMapRegistryManager::generateScopeId() {
  // This function should never be called multiple times for same scope.
  static bool initialized = false;
  RELEASE_ASSERT(!initialized,
                 "InlineMapRegistryManager::generateScopeId() called twice for same scope");

  const uint64_t scope_id = nextScopeId();
  mutableFinalizers().push_back([scope_id]() -> InlineMapRegistryDebugInfo {
    using Registry = InlineMapRegistry<Scope>;
    ASSERT(Registry::scopeId() == scope_id);
    return Registry::finalize();
  });

  // Mark the scope as initialized and the RELEASE_ASSERT will crash Envoy
  // if this function is called twice.
  initialized = true;

  return scope_id;
}

} // namespace Envoy
