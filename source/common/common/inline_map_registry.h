#pragma once

#include <cstddef>
#include <cstdint>
#include <list>

#include "envoy/common/optref.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {

/**
 * This library provide inline map templates which could be used as an alternative of normal hash
 * map to store the key/value pairs. But by these templates, you can register some frequently used
 * keys as inline keys and get the handle for the key. Then you can use the handle to access the
 * key/value pair in the map without even one time hash searching.
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
 *   // than all inline maps created by the registry. You can create static and global registry by
 *   // ``MUTABLE_CONSTRUCT_ON_FIRST_USE`` for cross-modules usage, but it is not required. A local
 *   // registry is also fine if the inline map is only used in the local scope.
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

/**
 * Inline map registry to store the inline keys and mapping of the inline keys to the inline
 * id.
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
  using RegistrationVector = std::vector<Key>;

  InlineMapRegistry() = default;

  /**
   * Register a custom inline key. May only be called before finalize().
   */
  template <class ArgK> InlineKey registerInlineKey(const ArgK& key) {
    RELEASE_ASSERT(!finalized_, "Cannot register inline key after finalize()");

    uint64_t inline_id = 0;

    if (auto it = registration_map_.find(key); it != registration_map_.end()) {
      // If the key is already registered, return the existing key.
      inline_id = it->second;
    } else {
      // If the key is not registered yet, then create a new inline key.
      inline_id = registration_map_.size();
      registration_vector_.push_back(Key(key));
      registration_map_[key] = inline_id;
    }

#ifndef NDEBUG
    return {inline_id, registry_id_};
#else
    return {inline_id};
#endif
  }

  /**
   * Fetch the inline handle for the given key. May only be called after finalize(). This should
   * be used to get the inline handle for the key registered by registerInlineKey(). This function
   * could used to determine if the key is registered or not at runtime or xDS config loading time
   * and decide if the key should be used as inline key or normal key.
   */
  template <class ArgK> absl::optional<InlineKey> getInlineKey(const ArgK& key) const {
    ASSERT(finalized_, "Cannot get inline handle before finalize()");

    uint64_t inline_id = 0;

    if (auto it = registration_map_.find(key); it == registration_map_.end()) {
      return absl::nullopt;
    } else {
      inline_id = it->second;
    }

#ifndef NDEBUG
    return InlineKey{inline_id, registry_id_};
#else
    return InlineKey{inline_id};
#endif
  }

  /**
   * Get the registration map that contains all registered inline keys. May only be called after
   * finalize().
   */
  const RegistrationMap& registrationMap() const {
    ASSERT(finalized_, "Cannot fetch registration map before finalize()");
    return registration_map_;
  }

  /**
   * Get the registration vector that contains all registered inline keys. May only be called after
   * finalize().
   */
  const RegistrationVector& registrationVector() const {
    ASSERT(finalized_, "Cannot fetch registration set before finalize()");
    return registration_vector_;
  }

  /**
   * Get the number of the registrations. May only be called after finalize().
   */
  uint64_t registrationNum() const {
    ASSERT(finalized_, "Cannot fetch registration map before finalize()");
    return registration_map_.size();
  }

  // Finalize this registry. No further changes are allowed after this point. This guaranteed that
  // all map created by the process have the same variable size and custom registrations. This
  // function could be called multiple times safely and only the first call will have effect.
  void finalize() { finalized_ = true; }

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
  RegistrationVector registration_vector_;
  RegistrationMap registration_map_;
};

/**
 * This is the inline map that could be used as an alternative of normal hash map to store the
 * key/value pairs.
 */
template <class Key, class Value> class InlineMap : public InlineStorage {
public:
  using TypedInlineMapRegistry = InlineMapRegistry<Key>;
  using InlineKey = typename TypedInlineMapRegistry::InlineKey;

  using UnderlayHashMap = absl::flat_hash_map<Key, Value>;
  struct InlineEntry {
    InlineEntry(Value&& value) : value_(std::move(value)) {}
    InlineEntry(const Value& value) : value_(value) {}

    Value value_;
    typename std::list<InlineEntry>::iterator entry_;
  };
  using UnderlayInlineEntryStorage = std::list<InlineEntry>;

  // Get the entry for the given key. If the key is not found, return nullptr.
  template <class ArgK> OptRef<const Value> lookup(const ArgK& key) const {
    // Because the normal string view key is used here, try the normal map entry first.
    if (auto it = dynamic_entries_.find(key); it != dynamic_entries_.end()) {
      return it->second;
    }

    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      if (auto entry = inline_entries_[*entry_id]; entry != nullptr) {
        return entry->value_;
      }
    }

    return {};
  }

  template <class ArgK> OptRef<Value> lookup(const ArgK& key) {
    // Because the normal string view key is used here, try the normal map entry first.
    if (auto it = dynamic_entries_.find(key); it != dynamic_entries_.end()) {
      return it->second;
    }

    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      if (auto entry = inline_entries_[*entry_id]; entry != nullptr) {
        return entry->value_;
      }
    }

    return {};
  }

  // Get the entry for the given inline key. If the handle is not valid, return nullptr.
  OptRef<const Value> lookup(InlineKey key) const {
#ifndef NDEBUG
    ASSERT(key.registryId() == registry_.registryId(), "Invalid inline key");
#endif
    if (auto entry = inline_entries_[key.inlineId()]; entry != nullptr) {
      return entry->value_;
    }
    return {};
  }

  OptRef<Value> lookup(InlineKey key) {
#ifndef NDEBUG
    ASSERT(key.registryId() == registry_.registryId(), "Invalid inline key");
#endif
    if (auto entry = inline_entries_[key.inlineId()]; entry != nullptr) {
      return entry->value_;
    }
    return {};
  }

  // Set the entry for the given key. If the key is already present, insert will fail and
  // return false. Otherwise, insert will succeed and return true.
  template <class ArgK, class ArgV> bool insert(ArgK&& key, ArgV&& value) {
    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      // This key is registered as inline key and try to insert the value to the inline array.

      // If the entry is already valid, insert will fail and return false.
      if (inline_entries_[*entry_id] != nullptr) {
        return false;
      }

      resetInlineMapEntry(*entry_id, std::move(value));
      return true;
    } else {
      // This key is not registered as inline key and try to insert the value to the normal map.
      return dynamic_entries_.emplace(std::forward<ArgK>(key), std::forward<ArgV>(value)).second;
    }
  }

  // Set the entry for the given handle. If the handle is not valid, do nothing.
  template <class ArgV> void insert(InlineKey key, ArgV&& value) {
#ifndef NDEBUG
    ASSERT(key.registryId() == registry_.registryId(), "Invalid inline key");
#endif
    resetInlineMapEntry(key.inlineId(), std::forward<ArgV>(value));
  }

  // Remove the entry for the given key. If the key is not found, do nothing.
  template <class ArgK> void remove(const ArgK& key) {
    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      clearInlineMapEntry(*entry_id);
    } else {
      dynamic_entries_.erase(key);
    }
  }

  // Remove the entry for the given handle. If the handle is not valid, do nothing.
  void remove(InlineKey key) {
#ifndef NDEBUG
    ASSERT(key.registryId() == registry_.registryId(), "Invalid inline key");
#endif
    clearInlineMapEntry(key.inlineId());
  }

  void iterate(std::function<bool(const Key&, const Value&)> callback) const {
    for (const auto& entry : dynamic_entries_) {
      if (!callback(entry.first, entry.second)) {
        return;
      }
    }

    for (const auto& id : registry_.registrationMap()) {
      ASSERT(id.second < registry_.registrationNum());

      auto entry = inline_entries_[id.second];
      if (entry == nullptr) {
        continue;
      }

      if (!callback(id.first, entry->value_)) {
        return;
      }
    }
  }

  uint64_t size() const { return dynamic_entries_.size() + inline_entries_storage_.size(); }

  bool empty() const { return size() == 0; }

  ~InlineMap() {
    for (uint64_t i = 0; i < registry_.registrationNum(); ++i) {
      clearInlineMapEntry(i);
    }
    memset(inline_entries_, 0, registry_.registrationNum() * sizeof(Value));
  }

  // Override operator [] to support the normal key assignment.
  template <class ArgK> Value& operator[](const ArgK& key) {
    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      // This key is registered as inline key and try to insert the value to the inline array.
      if (inline_entries_[*entry_id] != nullptr) {
        resetInlineMapEntry(*entry_id, Value());
      }
      return inline_entries_[*entry_id]->value_;
    } else {
      // This key is not registered as inline key and try to insert the value to the normal map.
      return dynamic_entries_[key];
    }
  }

  // Override operator [] to support the inline key assignment.
  Value& operator[](InlineKey key) {
#ifndef NDEBUG
    ASSERT(key.registryId() == registry_.registryId(), "Invalid inline key");
#endif

    if (inline_entries_[key.inlineId()] == nullptr) {
      resetInlineMapEntry(key.inlineId(), Value());
    }
    return inline_entries_[key.inlineId()]->value_;
  }

  static std::unique_ptr<InlineMap> create(TypedInlineMapRegistry& registry) {
    // Call finalize() anyway to make sure that the registry is finalized and no any new inline
    // keys could be registered.
    registry.finalize();

    return std::unique_ptr<InlineMap>(
        new ((registry.registrationMap().size() * sizeof(InlineEntry*))) InlineMap(registry));
  }

private:
  friend class InlineMapRegistry<Key>;

  InlineMap(TypedInlineMapRegistry& registry) : registry_(registry) {
    // Initialize the inline entries to nullptr.
    memset(inline_entries_, 0, registry_.registrationNum() * sizeof(InlineEntry*));
  }

  template <class ArgV> void resetInlineMapEntry(uint64_t inline_entry_id, ArgV&& value) {
    ASSERT(inline_entry_id < registry_.registrationNum());

    clearInlineMapEntry(inline_entry_id);

    auto iter =
        inline_entries_storage_.emplace(inline_entries_storage_.end(), std::forward<ArgV>(value));

    iter->entry_ = iter;
    inline_entries_[inline_entry_id] = &(*iter);
  }

  void clearInlineMapEntry(uint64_t inline_entry_id) {
    ASSERT(inline_entry_id < registry_.registrationNum());

    // Destruct the old entry if it is valid.
    if (auto entry = inline_entries_[inline_entry_id]; entry != nullptr) {
      inline_entries_storage_.erase(entry->entry_);
      inline_entries_[inline_entry_id] = nullptr;
    }
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
  const TypedInlineMapRegistry& registry_;

  // This is the underlay hash map for the normal entries.
  UnderlayHashMap dynamic_entries_;

  // This is the inline entries storage. The inline entries are stored in the list.
  UnderlayInlineEntryStorage inline_entries_storage_;

  // This should be the last member of the class and no member should be added after this.
  InlineEntry* inline_entries_[];
};

template <class Key, class Value> using InlineMapPtr = std::unique_ptr<InlineMap<Key, Value>>;

} // namespace Envoy
