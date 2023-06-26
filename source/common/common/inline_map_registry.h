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
 *   InlineMapRegistry<std::string>::InlineHandle inline_handle =
 *     registry.registerInlineKey("inline_key");
 *
 *   // Finalize the registry. No further changes are allowed to the registry after this point.
 *   registry.finalize();
 *
 *   // Create the inline map.
 *   InlineMapPtr inline_map = registry.createInlineMap<std::string>();
 *
 *   // Get value by inline handle.
 *   inline_map->insert(inline_handle, std::make_unique<std::string>("value"));
 *   EXPECT_EQ(*inline_map->lookup(inline_handle), "value");
 */
template <class Key> class InlineMapRegistry {
public:
  /**
   * This is the handle used to access the key/value pair in the inline map. The handle is
   * lightweight and could be copied and passed by value.
   */
  class InlineKey {
  public:
    /**
     * Get the id of the inline entry in the inline array. This could be used to access the
     * key/value pair in the inline map without hash searching.
     */
    uint64_t inlineId() const { return inline_id_; }

  private:
    friend class InlineMapRegistry;

    // This constructor should only be called by InlineMapRegistry.
    InlineKey(uint64_t inline_id) : inline_id_(inline_id) {}

    uint64_t inline_id_{};

#ifndef NDEBUG
  public:
    // This is the registry id that the inline key created by. This is used to validate the
    // inline key validity.
    uint64_t registryId() const { return registry_id_; }

  private:
    InlineKey(uint64_t inline_id, uint64_t registry_id)
        : inline_id_(inline_id), registry_id_(registry_id) {}
    uint64_t registry_id_{};
#endif
  };

  using RegistrationMap = absl::flat_hash_map<Key, uint64_t>;
  using RegistrationSet = std::vector<Key>;

  template <class T> class InlineMap : public InlineStorage {
  public:
    using TPtr = std::unique_ptr<T>;
    using UnderlayHashMap = absl::flat_hash_map<Key, TPtr>;

    // Get the entry for the given key. If the key is not found, return nullptr.
    template <class ArgK> const T* lookup(const ArgK& key) const { return lookupImpl(key); }
    template <class ArgK> T* lookup(const ArgK& key) { return lookupImpl(key); }

    // Get the entry for the given inline key. If the handle is not valid, return nullptr.
    const T* lookup(InlineKey key) const { return lookupImpl(key); }
    T* lookup(InlineKey key) { return lookupImpl(key); }

    // Set the entry for the given key. If the key is already present, overwrite it.
    template <class ArgK> void insert(const ArgK& key, TPtr value) {
      if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
        resetInlineMapEntry(*entry_id, std::move(value));
      } else {
        dynamic_entries_[key] = std::move(value);
      }
    }

    // Set the entry for the given handle. If the handle is not valid, do nothing.
    void insert(InlineKey key, TPtr value) {
#ifndef NDEBUG
      ASSERT(key.registryId() == registry_.registryId(), "Invalid inline key");
#endif
      resetInlineMapEntry(key.inlineId(), std::move(value));
    }

    // Remove the entry for the given key. If the key is not found, do nothing.
    template <class ArgK> void remove(const ArgK& key) {
      if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
        resetInlineMapEntry(*entry_id);
      } else {
        dynamic_entries_.erase(key);
      }
    }

    // Remove the entry for the given handle. If the handle is not valid, do nothing.
    void remove(InlineKey key) {
#ifndef NDEBUG
      ASSERT(key.registryId() == registry_.registryId(), "Invalid inline key");
#endif
      resetInlineMapEntry(key.inlineId());
    }

    void iterate(std::function<bool(const Key&, T*)> callback) const {
      for (const auto& entry : dynamic_entries_) {
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

        if (!callback(id.first, entry)) {
          return;
        }
      }
    }

    uint64_t size() const { return dynamic_entries_.size() + inline_entries_size_; }

    bool empty() const { return size() == 0; }

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

    template <class ArgK> T* lookupImpl(const ArgK& key) const {
      // Because the normal string view key is used here, try the normal map entry first.
      if (auto it = dynamic_entries_.find(key); it != dynamic_entries_.end()) {
        return it->second.get();
      }

      if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
        return inline_entries_[*entry_id];
      }

      return nullptr;
    }

    T* lookupImpl(InlineKey key) const {
#ifndef NDEBUG
      ASSERT(key.registryId() == registry_.registryId(), "Invalid inline key");
#endif
      return inline_entries_[key.inlineId()];
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

    template <class ArgK> absl::optional<uint64_t> inlineLookup(const ArgK& key) const {
      const auto& map_ref = registry_.registrationMap();
      if (auto iter = map_ref.find(key); iter != map_ref.end()) {
        return iter->second;
      }
      return absl::nullopt;
    }

    // This is the reference of the registry that the inline map created by. This is used to
    // validate the inline handle validity and get the inline key set.
    const InlineMapRegistry& registry_;

    // This is the underlay hash map for the normal entries.
    UnderlayHashMap dynamic_entries_;

    uint64_t inline_entries_size_{};
    // This should be the last member of the class and no member should be added after this.
    T* inline_entries_[];
  };

  template <class T> using InlineMapPtr = std::unique_ptr<InlineMap<T>>;

  InlineMapRegistry() = default;

  /**
   * Register a custom inline key. May only be called before finalized().
   */
  template <class ArgK> InlineKey registerInlineKey(const ArgK& key) {
    RELEASE_ASSERT(!finalized_, "Cannot register inline key after finalized()");

    uint64_t inline_id = 0;

    if (auto it = registration_map_.find(key); it != registration_map_.end()) {
      // If the key is already registered, return the existing key.
      inline_id = it->second;
    } else {
      // If the key is not registered yet, then create a new inline key.
      inline_id = registration_map_.size();
      registration_set_.push_back(Key(key));
      registration_map_[key] = inline_id;
    }

#ifndef NDEBUG
    return {inline_id, registry_id_};
#else
    return {inline_id};
#endif
  }

  /**
   * Fetch the inline handle for the given key. May only be called after finalized(). This should
   * be used to get the inline handle for the key registered by registerInlineKey(). This function
   * could used to determine if the key is registered or not at runtime or xDS config loading time
   * and decide if the key should be used as inline key or normal key.
   */
  template <class ArgK> absl::optional<InlineKey> getInlineKey(const ArgK& key) const {
    ASSERT(finalized_, "Cannot get inline handle before finalized()");

    uint64_t inline_id = 0;

    if (auto it = registration_map_.find(key); it == registration_map_.end()) {
      return absl::nullopt;
    } else {
      inline_id = it->second;
    }

#ifndef NDEBUG
    return InlineKey{inline_id, registry_id_};
#else
    return InlineKey{it->second};
#endif
  }

  /**
   * Get the registration map that contains all registered inline keys. May only be called after
   * finalized().
   */
  const RegistrationMap& registrationMap() const {
    ASSERT(finalized_, "Cannot fetch registration map before finalized()");
    return registration_map_;
  }

  /**
   * Get the registration set that contains all registered inline keys. May only be called after
   * finalized().
   */
  const RegistrationSet& registrationSet() const {
    ASSERT(finalized_, "Cannot fetch registration set before finalized()");
    return registration_set_;
  }

  /**
   * Get the number of the registrations. May only be called after finalized().
   */
  uint64_t registrationNum() const {
    ASSERT(finalized_, "Cannot fetch registration map before finalized()");
    return registration_map_.size();
  }

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

#ifndef NDEBUG
public:
  // This is the registry id that the inline map created by. This is used to validate the inline
  // key validity.
  uint64_t registryId() const { return registry_id_; }

private:
  static uint64_t generateRegistryId() {
    static std::atomic<uint64_t> next_id{0};
    return next_id++;
  }

  uint64_t registry_id_{generateRegistryId()};
#endif

private:
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
  using InlineKey = InlineMapRegistry<std::string>::InlineKey;
  using ManagedRegistries = absl::flat_hash_map<std::string, InlineMapRegistry<std::string>*>;

  /**
   * Get the inline map registry singleton based on the scope.
   */
  template <class Scope>
  static InlineMapRegistry<std::string>::InlineKey registerInlinKey(absl::string_view key) {
    return mutableRegistry<Scope>().registerInlineKey(key);
  }

  /**
   * Get registered inline key by the key name.
   */
  template <class Scope>
  static absl::optional<InlineMapRegistry<std::string>::InlineKey>
  getInlineKey(absl::string_view key) {
    return mutableRegistry<Scope>().getInlineKey(key);
  }

  /**
   * Create inline map based on the scope.
   */
  template <class Scope, class T>
  static InlineMapRegistry<std::string>::InlineMapPtr<T> createInlineMap() {
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
  template <class Scope> static InlineMapRegistry<std::string>& mutableRegistry() {
    static InlineMapRegistry<std::string>* registry = []() {
      auto* registry = new InlineMapRegistry<std::string>();
      auto result = InlineMapRegistryHelper::mutableRegistries().emplace(Scope::name(), registry);
      RELEASE_ASSERT(result.second,
                     absl::StrCat("Duplicate inline map registry for scope: ", Scope::name()));
      return registry;
    }();
    return *registry;
  }

  static ManagedRegistries& mutableRegistries() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(ManagedRegistries);
  }
};

} // namespace Envoy
