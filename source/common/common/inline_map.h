#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>

#include "envoy/common/optref.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {

/**
 * This library provide inline map templates which could be used as an alternative to normal hash
 * maps. But by these templates, you can add some frequently used
 * keys as inline keys and get the handle for the key. Then you can use the handle to access the
 * key/value pair in the map without even one time hash searching.
 *
 * This is useful when some keys are frequently used and the keys are known at compile time or
 * bootstrap time. You can get superior performance by using the handle. For example, the
 * filter state always uses the filter name as the key and the filter name is known at compile time.
 * By using the handle, the filter state could get the key/value pair without any hash.
 *
 * This class also provides the interface to access the key/value pair by the normal key and the
 * interface has similar searching performance as the normal hash map. But the insertion and removal
 * by the normal key is slower than the normal hash map.
 *
 *
 * These is usage example:
 *
 *   // Define descriptor to store the inline keys. The descriptor should have lifetime that no
 *   // shorter than all inline maps created by the descriptor. You can create static and global
 *   // descriptor by ``MUTABLE_CONSTRUCT_ON_FIRST_USE`` for cross-modules usage, but it is not
 *   // required. A local descriptor is also fine if the inline map is only used in the local scope.
 *   InlineMapDescriptor<std::string> descriptor;
 *
 *   // Create the handle for inline key. We should never do this after bootstrapping. Typically, we
 *   // can do this by define a global variable.
 *   InlineMapDescriptor<std::string>::Handle handle = descriptor.addHandle("inline_key");
 *
 *   // Finalize the descriptor. No further changes are allowed to the descriptor after this point.
 *   descriptor.finalize();
 *
 *   // Create the inline map.
 *   InlineMapPtr<std::string, std::string> inline_map = InlineMap<std::string,
 *     std::string>::create(descriptor);
 *
 *   // Get value by handle.
 *   inline_map->insert(handle, "value");
 *   EXPECT_EQ(*inline_map->lookup(handle), "value");
 */

/**
 * Inline map descriptor to store the inline keys and mapping of the inline keys to the inline
 * id.
 */
template <class Key> class InlineMapDescriptor {
public:
  /**
   * This is the handle used to access the key/value pair in the inline map. The handle is
   * lightweight and could be copied and passed by value.
   */
  class Handle {
  public:
    /**
     * Get the id of the inline entry in the inline array. This could be used to access the
     * key/value pair in the inline map without hash searching.
     */
    uint64_t inlineId() const { return inline_id_; }

  private:
    friend class InlineMapDescriptor;

    // This constructor should only be called by InlineMapDescriptor.
    Handle(uint64_t inline_id) : inline_id_(inline_id) {}

    uint64_t inline_id_{};
  };

  using InlineKeysMap = absl::flat_hash_map<Key, Handle>;
  using InlineKeys = std::vector<Key>;

  InlineMapDescriptor() = default;

  /**
   * Add an inline key and return related handle. May only be called before finalize().
   */
  template <class ArgK> Handle addInlineKey(const ArgK& key) {
    RELEASE_ASSERT(!finalized_, "Cannot create new inline key after finalize()");

    if (auto it = inline_keys_map_.find(key); it != inline_keys_map_.end()) {
      // If the key is already added, return related inline handle directly.
      return it->second;
    } else {
      // If the key is not added yet, then create a new handle for this key.
      Handle handle{inline_keys_map_.size()};
      inline_keys_.push_back(Key(key));
      inline_keys_map_.emplace(key, handle);
      return handle;
    }
  }

  /**
   * Fetch the handle for the given key. Return handle if the given key is inline key and return
   * absl::nullopt if the given key is normal key. May only be called after finalize(). This should
   * be used to get the handle of the inline keys that added by addHandle(). This function could
   * used to determine if a key is added as inline key or not at runtime or xDS config loading time
   * and decide if the key should be used as inline key or normal key.
   */
  template <class ArgK> absl::optional<Handle> getHandleByKey(const ArgK& key) const {
    ASSERT(finalized_, "Cannot get inline handle before finalize()");

    if (auto it = inline_keys_map_.find(key); it == inline_keys_map_.end()) {
      return absl::nullopt;
    } else {
      return it->second;
    }
  }

  /**
   * Get the inline keys map that contains all inline keys and their handle. May only be called
   * after finalize().
   */
  const InlineKeysMap& inlineKeysMap() const {
    ASSERT(finalized_, "Cannot fetch registration map before finalize()");
    return inline_keys_map_;
  }

  /**
   * Get the array that contains all added inline keys. May only be called after finalize().
   */
  const InlineKeys& inlineKeys() const {
    ASSERT(finalized_, "Cannot fetch registration set before finalize()");
    return inline_keys_;
  }

  /**
   * Get the number of the inline keys in this descriptor. May only be called after finalize().
   */
  uint64_t inlineKeysNum() const {
    ASSERT(finalized_, "Cannot fetch registration map before finalize()");
    return inline_keys_map_.size();
  }

  // Finalize this descriptor. No further changes are allowed after this point. This guaranteed that
  // all map created by the process have the same variable size and custom inline key adding. This
  // function could be called multiple times safely and only the first call will have effect.
  void finalize() { finalized_ = true; }

private:
  bool finalized_{};
  InlineKeys inline_keys_;
  InlineKeysMap inline_keys_map_;
};

/**
 * This is the inline map that could be used as an alternative of normal hash map to store the
 * key/value pairs.
 */
template <class Key, class Value> class InlineMap : public InlineStorage {
public:
  using TypedInlineMapDescriptor = InlineMapDescriptor<Key>;
  using Handle = typename TypedInlineMapDescriptor::Handle;

  using UnderlayHashMap = absl::flat_hash_map<Key, Value>;
  using ValueRef = std::reference_wrapper<Value>;

  /**
   * Get the entry by the given key. Heterogeneous lookup is supported here.
   * @param key the key to lookup.
   * @return the entry if the key is found, otherwise return null reference.
   */
  template <class ArgK> OptRef<const Value> lookup(const ArgK& key) const {
    // Because the normal string view key is used here, try the normal map entry first.
    if (auto it = dynamic_entries_.find(key); it != dynamic_entries_.end()) {
      return it->second;
    }

    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      if (inlineEntryValid(*entry_id)) {
        return inline_entries_[*entry_id];
      }
    }

    return {};
  }

  /**
   * Get the entry by the given key. Heterogeneous lookup is supported here.
   * @param key the key to lookup.
   * @return the entry if the key is found, otherwise return null reference.
   */
  template <class ArgK> OptRef<Value> lookup(const ArgK& key) {
    // Because the normal string view key is used here, try the normal map entry first.
    if (auto it = dynamic_entries_.find(key); it != dynamic_entries_.end()) {
      return it->second;
    }

    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      if (inlineEntryValid(*entry_id)) {
        return inline_entries_[*entry_id];
      }
    }

    return {};
  }

  /**
   * Get the entry by the given handle.
   * @param handle the handle to lookup.
   * @return the entry if the handle is valid, otherwise return null reference.
   */
  OptRef<const Value> lookup(Handle handle) const {
    if (inlineEntryValid(handle.inlineId())) {
      return inline_entries_[handle.inlineId()];
    }
    return {};
  }

  /**
   * Get the entry by the given handle.
   * @param handle the handle to lookup.
   * @return the entry if the handle is valid, otherwise return null reference.
   */
  OptRef<Value> lookup(Handle handle) {
    if (inlineEntryValid(handle.inlineId())) {
      return inline_entries_[handle.inlineId()];
    }
    return {};
  }

  /**
   * Set the entry by the given key.
   * @param key the key to set.
   * @param value the value to set.
   * @return pair consisting of a reference to the inserted element, or the already-existing element
   * if no insertion happened, and a bool denoting whether the insertion took place (true if
   * insertion happened, false if it did not).
   */
  template <class ArgK, class ArgV> std::pair<ValueRef, bool> insert(ArgK&& key, ArgV&& value) {
    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      // This key is registered as inline key and try to insert the value to the inline array.

      // If the entry is already valid, insert will fail and return the ref of exist value.
      if (inlineEntryValid(*entry_id)) {
        return {inline_entries_[*entry_id], false};
      }

      resetInlineMapEntry(*entry_id, std::forward<ArgV>(value));
      return {inline_entries_[*entry_id], true};
    } else {
      // This key is not registered as inline key and try to insert the value to the normal map.
      auto result = dynamic_entries_.emplace(std::forward<ArgK>(key), std::forward<ArgV>(value));
      return {result.first->second, result.second};
    }
  }

  /**
   * Set the entry by the given handle.
   * @param handle the handle to set.
   * @param value the value to set.
   * @return pair consisting of a reference to the inserted element, or the already-existing element
   * if no insertion happened, and a bool denoting whether the insertion took place (true if
   * insertion happened, false if it did not).
   */
  template <class ArgV> std::pair<ValueRef, bool> insert(Handle handle, ArgV&& value) {
    // If the entry is already valid, insert will fail and return false.
    if (inlineEntryValid(handle.inlineId())) {
      return {inline_entries_[handle.inlineId()], false};
    }

    resetInlineMapEntry(handle.inlineId(), std::forward<ArgV>(value));
    return {inline_entries_[handle.inlineId()], true};
  }

  // Remove the entry by the given key. If the key is not found, do nothing.
  template <class ArgK> void remove(const ArgK& key) {
    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      clearInlineMapEntry(*entry_id);
    } else {
      dynamic_entries_.erase(key);
    }
  }

  // Remove the entry by the given handle. If the handle is not valid, do nothing.
  void remove(Handle handle) { clearInlineMapEntry(handle.inlineId()); }

  void iterate(std::function<bool(const Key&, const Value&)> callback) const {
    for (const auto& entry : dynamic_entries_) {
      if (!callback(entry.first, entry.second)) {
        return;
      }
    }

    for (uint64_t i = 0; i < descriptor_.inlineKeysNum(); ++i) {
      if (!inlineEntryValid(i)) {
        continue;
      }

      if (!callback(descriptor_.inlineKeys()[i], inline_entries_[i])) {
        return;
      }
    }
  }

  uint64_t size() const { return dynamic_entries_.size() + inline_entries_size_; }

  bool empty() const { return size() == 0; }

  ~InlineMap() {
    // Destruct all inline entries.
    for (uint64_t i = 0; i < descriptor_.inlineKeysNum(); ++i) {
      clearInlineMapEntry(i);
    }
    memset(inline_entries_storage_, 0, descriptor_.inlineKeysNum() * sizeof(Value));
  };

  // Override operator [] to support the normal key assignment.
  template <class ArgK> Value& operator[](const ArgK& key) {
    if (auto entry_id = inlineLookup(key); entry_id.has_value()) {
      // This key is registered as inline key and try to insert the value to the inline array.
      if (!inlineEntryValid(*entry_id)) {
        resetInlineMapEntry(*entry_id, Value());
      }
      return inline_entries_[*entry_id];
    } else {
      // This key is not registered as inline key and try to insert the value to the normal map.
      return dynamic_entries_[key];
    }
  }

  // Override operator [] to support the handle of inline key assignment.
  Value& operator[](Handle handle) {
    if (!inlineEntryValid(handle.inlineId())) {
      resetInlineMapEntry(handle.inlineId(), Value());
    }
    return inline_entries_[handle.inlineId()];
  }

  static std::unique_ptr<InlineMap> create(TypedInlineMapDescriptor& registry) {
    // Call finalize() anyway to make sure that the registry is finalized and no any new inline
    // keys could be registered.
    registry.finalize();

    return std::unique_ptr<InlineMap>(new ((registry.inlineKeysNum() * sizeof(Value)))
                                          InlineMap(registry));
  }

private:
  friend class InlineMapDescriptor<Key>;

  InlineMap(TypedInlineMapDescriptor& registry) : descriptor_(registry) {
    // Initialize the inline entries to nullptr.
    uint64_t inline_keys_num = descriptor_.inlineKeysNum();
    inline_entries_valid_.resize(inline_keys_num, false);
    memset(inline_entries_storage_, 0, inline_keys_num * sizeof(Value));
    inline_entries_ = reinterpret_cast<Value*>(inline_entries_storage_);
  }

  template <class ArgV> void resetInlineMapEntry(uint64_t inline_entry_id, ArgV&& value) {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());

    clearInlineMapEntry(inline_entry_id);

    // Construct the new entry in the inline array.
    new (inline_entries_ + inline_entry_id) Value(std::forward<ArgV>(value));
    inlineEntryValid(inline_entry_id, true);
  }

  void clearInlineMapEntry(uint64_t inline_entry_id) {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());

    // Destruct the old entry if it is valid.
    if (inlineEntryValid(inline_entry_id)) {
      (inline_entries_ + inline_entry_id)->~Value();
      inlineEntryValid(inline_entry_id, false);
    }
  }

  template <class ArgK> absl::optional<uint64_t> inlineLookup(const ArgK& key) const {
    const auto& map_ref = descriptor_.inlineKeysMap();
    if (auto iter = map_ref.find(key); iter != map_ref.end()) {
      return iter->second.inlineId();
    }
    return absl::nullopt;
  }

  bool inlineEntryValid(uint64_t inline_entry_id) const {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());
    return inline_entries_valid_[inline_entry_id];
  }
  void inlineEntryValid(uint64_t inline_entry_id, bool flag) {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());

    if (flag) {
      inline_entries_size_++;
    } else {
      ASSERT(inline_entries_size_ > 0);
      inline_entries_size_--;
    }

    inline_entries_valid_[inline_entry_id] = flag;
  }

  // This is the reference of the registry that the inline map created by. This is used to
  // validate the inline handle validity and get the inline key set.
  const TypedInlineMapDescriptor& descriptor_;

  // This is the underlay hash map for the normal entries.
  UnderlayHashMap dynamic_entries_;

  // This is flags to indicate if the inline entries are valid.
  // TODO(wbpcode): use memory in the storage to store the flags.
  std::vector<bool> inline_entries_valid_;

  uint64_t inline_entries_size_{};

  Value* inline_entries_{};

  // This should be the last member of the class and no member should be added after this.
  uint8_t inline_entries_storage_[];
};

template <class Key, class Value> using InlineMapPtr = std::unique_ptr<InlineMap<Key, Value>>;

} // namespace Envoy
