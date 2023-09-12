#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>

#include "envoy/common/optref.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/strings/str_join.h"

namespace Envoy {

/**
 * This file contains classes to help make very fast maps where a subset of frequently used keys are
 * known before the map is constructed.
 *
 * For example, the filter state always uses the filter name as the key and the filter name is
 * known at compile time.
 * By using the these classes, the filter state could get the key/value pair without any hash.
 *
 * Dynamic key lookups are also supported and have slightly worse performance than
 * absl::flat_hash_map. Using this class makes sense if most of the lookups are expected to use the
 * predefined handles.
 *
 *
 * Example:
 *
 *   // Establishes a descriptor defining fast handles for the inline map. The descriptor must
 *   // outlive maps defined using it. One possible usage model is to lazy-create statics for a
 *   // descriptor and its handles using MUTABLE_CONSTRUCT_ON_FIRST_USE.
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
 *   InlineMap<std::string, std::string> inline_map(descriptor)
 *
 *   // Set value by handle.
 *   inline_map.set(handle, "value");
 *   EXPECT_EQ(*inline_map.get(handle), "value");
 */

/**
 * Maintains a collection of lightweight handles use for fast lookups into an inline map.
 */
template <class StorageKey> class InlineMapDescriptor {
public:
  /**
   * Holds a fast index to a map. This is guaranteed to be small and cheap to copy and store.
   */
  class Handle {
  public:
    bool operator==(const Handle& rhs) const { return inline_id_ == rhs.inline_id_; }
    bool operator!=(const Handle& rhs) const { return inline_id_ != rhs.inline_id_; }

  private:
    friend class InlineMapDescriptor;
    template <class Key, class Value> friend class InlineMap;

    /**
     * Get the id of the inline entry in the inline array. This could be used to access the
     * key/value pair in the inline map without hash searching.
     */
    uint32_t inlineId() const { return inline_id_; }

    // This constructor should only be called by InlineMapDescriptor.
    Handle(uint32_t inline_id) : inline_id_(inline_id) {}

    const uint32_t inline_id_;
  };

  InlineMapDescriptor() = default;

  /**
   * Add an inline key and return related handle. If a same key is added more than once time the
   * same handle will returned for the key. May only be called before finalize(). Heterogeneous
   * lookup is supported here.
   */
  template <class GetKey> Handle addInlineKey(const GetKey& key) {
    RELEASE_ASSERT(!finalized_, "Cannot create new inline key after finalize()");

    if (auto it = inline_keys_map_.find(key); it != inline_keys_map_.end()) {
      // If the key is already added, return related inline handle directly.
      return it->second;
    } else {
      // If the key is not added yet, then create a new handle for this key.
      ASSERT(inline_keys_map_.size() < std::numeric_limits<uint32_t>::max());
      Handle handle{static_cast<uint32_t>(inline_keys_map_.size())};
      inline_keys_.push_back(StorageKey(key));
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
   * Heterogeneous lookup is supported here.
   */
  template <class GetKey> absl::optional<Handle> getHandleByKey(const GetKey& key) const {
    ASSERT(finalized_, "Cannot get inline handle before finalize()");

    if (auto it = inline_keys_map_.find(key); it == inline_keys_map_.end()) {
      return absl::nullopt;
    } else {
      return it->second;
    }
  }

  /**
   * Finalize this descriptor. No further changes are allowed after this point. This guarantees that
   * all map created by the process have the same variable size and custom inline key adding. This
   * method should only be called once.
   */
  void finalize() {
    ASSERT(!finalized_, "Cannot finalize() an already finalized descriptor");
    finalized_ = true;
  }

  /**
   * @return true if the descriptor is finalized.
   */
  bool finalized() const { return finalized_; }

  /**
   * @return all inline keys that joined by the given separator.
   */
  std::string inlineKeysAsString(absl::string_view separator = ",") const {
    ASSERT(finalized_, "Cannot fetch debug string before finalize()");
    return absl::StrJoin(inline_keys_, separator);
  }

private:
  template <class Key, class Value> friend class InlineMap;

  using Hash = absl::container_internal::hash_default_hash<StorageKey>;
  using InlineKeysMap = absl::flat_hash_map<StorageKey, Handle, Hash>;
  using InlineKeys = std::vector<StorageKey>;

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

  bool finalized_{};
  InlineKeys inline_keys_;
  InlineKeysMap inline_keys_map_;
};

/**
 * This is the inline map that could be used as an alternative of normal hash map to store the
 * key/value pairs.
 */
template <class Key, class Value> class InlineMap {
private:
  using Descriptor = InlineMapDescriptor<Key>;
  using Hash = typename Descriptor::Hash;
  using DynamicHashMap = absl::flat_hash_map<Key, Value, Hash>;

public:
  using Handle = typename Descriptor::Handle;
  using ValueRef = std::reference_wrapper<Value>;

  /**
   * Construct an inline map with the given descriptor.
   */
  InlineMap(const Descriptor& descriptor) : descriptor_(descriptor) {
    ASSERT(descriptor_.finalized(), "Cannot create inline map before finalize()");
  }

  /**
   * Construct an inline map by moving another inline map.
   */
  InlineMap(InlineMap&& rhs) noexcept
      : descriptor_(rhs.descriptor_), dynamic_entries_(std::move(rhs.dynamic_entries_)),
        inline_entries_(std::move(rhs.inline_entries_)),
        inline_entries_size_(rhs.inline_entries_size_),
        inline_entries_valid_(rhs.inline_entries_valid_) {

    // Clear the moved map.
    rhs.inline_entries_size_ = 0;
    rhs.inline_entries_valid_ = nullptr;
  }

  ~InlineMap() { clear(); }

  /**
   * Get the entry by the given key. Heterogeneous lookup is supported here.
   * @param key the key to lookup.
   * @return the entry if the key is found, otherwise returns an OptRef with has_value() == false.
   */
  template <class GetKey> OptRef<const Value> get(const GetKey& key) const {
    // Compute the hash value here to avoid computing it twice in the dynamic map and inline keys
    // map.
    const size_t hash = Hash{}(key);

    // Because the dynamic key is used here, try the normal map entry first.
    if (const auto it = dynamic_entries_.find(key, hash); it != dynamic_entries_.end()) {
      return it->second;
    }

    if (absl::optional<uint64_t> entry_id = inlineLookup(key, hash); entry_id.has_value()) {
      if (inlineEntryValid(*entry_id)) {
        return inlineEntries()[*entry_id];
      }
    }

    return {};
  }

  /**
   * Get the entry by the given key. Heterogeneous lookup is supported here.
   * @param key the key to lookup.
   * @return the entry if the key is found, otherwise returns an OptRef with has_value() == false.
   */
  template <class GetKey> OptRef<Value> get(const GetKey& key) {
    // Compute the hash value here to avoid computing it twice in the dynamic map and inline keys
    // map.
    const size_t hash = Hash{}(key);

    // Because the dynamic key is used here, try the normal map entry first.
    if (auto it = dynamic_entries_.find(key, hash); it != dynamic_entries_.end()) {
      return it->second;
    }

    if (absl::optional<uint64_t> entry_id = inlineLookup(key, hash); entry_id.has_value()) {
      if (inlineEntryValid(*entry_id)) {
        return inlineEntries()[*entry_id];
      }
    }

    return {};
  }

  /**
   * Get the entry by the given handle.
   * @param handle the handle to lookup.
   * @return the entry if the handle is valid, otherwise returns an OptRef with has_value() ==
   * false.
   */
  OptRef<const Value> get(Handle handle) const {
    if (inlineEntryValid(handle.inlineId())) {
      return inlineEntries()[handle.inlineId()];
    }
    return {};
  }

  /**
   * Get the entry by the given handle.
   * @param handle the handle to lookup.
   * @return the entry if the handle is valid, otherwise returns an OptRef with has_value() ==
   * false.
   */
  OptRef<Value> get(Handle handle) {
    if (inlineEntryValid(handle.inlineId())) {
      return inlineEntries()[handle.inlineId()];
    }
    return {};
  }

  /**
   * Set the entry by the given key.
   * @param key the key to set.
   * @param value the value to set.
   * @return pair consisting of a reference to the element being set, or the already-existing
   * element if no setting happened, and a bool denoting whether the setting took place (true if
   * setting happened, false if it did not).
   */
  template <class SetKey, class SetValue>
  std::pair<ValueRef, bool> set(SetKey&& key, SetValue&& value) {
    if (absl::optional<uint64_t> entry_id = inlineLookup(key); entry_id.has_value()) {
      // This key is registered as inline key and try to insert the value to the inline array.

      // If the entry is already valid, insert will fail and return the ref of exist value.
      if (inlineEntryValid(*entry_id)) {
        return {inlineEntries()[*entry_id], false};
      }

      resetInlineMapEntry(*entry_id, std::forward<SetValue>(value));
      return {inlineEntries()[*entry_id], true};
    } else {
      // This key is not registered as inline key and try to insert the value to the normal map.
      auto result =
          dynamic_entries_.emplace(std::forward<SetKey>(key), std::forward<SetValue>(value));
      return {result.first->second, result.second};
    }
  }

  /**
   * Set the entry by the given handle.
   * @param handle the handle to set.
   * @param value the value to set.
   * @return pair consisting of a reference to the element being set, or the already-existing
   * element if no setting happened, and a bool denoting whether the setting took place (true if
   * setting happened, false if it did not).
   */
  template <class SetValue> std::pair<ValueRef, bool> set(Handle handle, SetValue&& value) {
    // If the entry is already valid, insert will fail and return false.
    if (inlineEntryValid(handle.inlineId())) {
      return {inlineEntries()[handle.inlineId()], false};
    }

    resetInlineMapEntry(handle.inlineId(), std::forward<SetValue>(value));
    return {inlineEntries()[handle.inlineId()], true};
  }

  /**
   * Erase the entry by the given key. If the key is not found, do nothing.
   * Heterogeneous lookup is supported here.
   * @param key the key to erase.
   * @return the number of elements erased.
   */
  template <class GetKey> uint64_t erase(const GetKey& key) {
    if (absl::optional<uint64_t> entry_id = inlineLookup(key); entry_id.has_value()) {
      return clearInlineMapEntry(*entry_id);
    } else {
      return dynamic_entries_.erase(key);
    }
  }

  /**
   * Erase the entry by the given handle. If the handle is not valid, do nothing.
   * @param handle the handle to erase.
   * @return the number of elements erased.
   */
  uint64_t erase(Handle handle) { return clearInlineMapEntry(handle.inlineId()); }

  /**
   * Iterate all elements in the map.
   * @param callback the callback function to handle each element.
   */
  void iterate(std::function<bool(const Key&, const Value&)> callback) const {
    for (const auto& entry : dynamic_entries_) {
      if (!callback(entry.first, entry.second)) {
        return;
      }
    }

    const uint64_t inline_keys_num = descriptor_.inlineKeysNum();
    const auto& inline_keys = descriptor_.inlineKeys();
    const Value* inline_entries = inlineEntries();
    for (uint64_t i = 0; i < inline_keys_num; ++i) {
      if (!inlineEntryValid(i)) {
        continue;
      }

      if (!callback(inline_keys[i], inline_entries[i])) {
        return;
      }
    }
  }

  /**
   * Clear all elements in the map.
   */
  void clear() {
    // Clear the normal dynamic map.
    dynamic_entries_.clear();

    // Quick return if the inline entries is not allocated to avoid
    // unnecessary iterations.
    if (inline_entries_ == nullptr) {
      return;
    }

    const uint64_t inline_keys_num = descriptor_.inlineKeysNum();
    for (uint64_t i = 0; i < inline_keys_num; ++i) {
      clearInlineMapEntry(i);
    }
  }

  /**
   * @return the number of elements in the map.
   */
  uint64_t size() const { return dynamic_entries_.size() + inline_entries_size_; }

  /**
   * @return true if the map is empty.
   */
  bool empty() const { return size() == 0; }

  /**
   * Override operator [] to support the dynamic key assignment.
   */
  template <class GetKey> Value& operator[](const GetKey& key) {
    if (absl::optional<uint64_t> entry_id = inlineLookup(key); entry_id.has_value()) {
      // This key is registered as inline key and try to add the value to the inline array.
      if (!inlineEntryValid(*entry_id)) {
        resetInlineMapEntry(*entry_id, Value());
      }
      return inlineEntries()[*entry_id];
    } else {
      // This key is not registered as inline key and try to add the value to the normal map.
      return dynamic_entries_[key];
    }
  }

  /**
   * Override operator [] to support the inline handle assignment.
   */
  Value& operator[](Handle handle) {
    if (!inlineEntryValid(handle.inlineId())) {
      resetInlineMapEntry(handle.inlineId(), Value());
    }
    return inlineEntries()[handle.inlineId()];
  }

private:
  using StoragePtr = std::unique_ptr<uint8_t[]>;
  friend class InlineMapDescriptor<Key>;

  void lazyInitializeInlineEntriesMemory() {
    if (inline_entries_ == nullptr) {
      const uint64_t value_memory_size = descriptor_.inlineKeysNum() * sizeof(Value);
      const uint64_t valid_memory_size = descriptor_.inlineKeysNum() * sizeof(bool);
      const uint64_t memory_size = value_memory_size + valid_memory_size;

      inline_entries_.reset(new uint8_t[memory_size]);
      memset(inline_entries_.get(), 0, memory_size);

      inline_entries_valid_ = reinterpret_cast<bool*>(inline_entries_.get() + value_memory_size);
    }
  }

  template <class SetValue> void resetInlineMapEntry(uint64_t inline_entry_id, SetValue&& value) {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());
    // Only allocate the memory for inline entries when the first inline entry is added. The avoid
    // the unnecessary memory allocation and deallocation for the map instances that never add any
    // inline entries.
    lazyInitializeInlineEntriesMemory();

    clearInlineMapEntry(inline_entry_id);

    // Construct the new entry in the inline array.
    new (inlineEntries() + inline_entry_id) Value(std::forward<SetValue>(value));

    setInlineEntryValid(inline_entry_id, true);
  }

  uint64_t clearInlineMapEntry(uint64_t inline_entry_id) {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());

    // Destruct the old entry if it is valid.
    if (inlineEntryValid(inline_entry_id)) {
      setInlineEntryValid(inline_entry_id, false);
      (inlineEntries() + inline_entry_id)->~Value();
      return 1;
    }

    return 0;
  }

  template <class GetKey> absl::optional<uint64_t> inlineLookup(const GetKey& key) const {
    const auto& map_ref = descriptor_.inlineKeysMap();
    if (auto iter = map_ref.find(key); iter != map_ref.end()) {
      return iter->second.inlineId();
    }
    return absl::nullopt;
  }

  template <class GetKey>
  absl::optional<uint64_t> inlineLookup(const GetKey& key, size_t hash) const {
    const auto& map_ref = descriptor_.inlineKeysMap();
    if (auto iter = map_ref.find(key, hash); iter != map_ref.end()) {
      return iter->second.inlineId();
    }
    return absl::nullopt;
  }

  bool inlineEntryValid(uint64_t inline_entry_id) const {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());

    if (inline_entries_ == nullptr) {
      // If the inline entries is not allocated, then the inline entry is not valid.
      return false;
    }
    return inline_entries_valid_[inline_entry_id];
  }
  void setInlineEntryValid(uint64_t inline_entry_id, bool flag) {
    ASSERT(inline_entry_id < descriptor_.inlineKeysNum());
    ASSERT(inline_entries_ != nullptr);

    if (flag) {
      inline_entries_size_++;
    } else {
      ASSERT(inline_entries_size_ > 0);
      inline_entries_size_--;
    }

    inline_entries_valid_[inline_entry_id] = flag;
  }

  // Helper function to get the inline entries array by the inline entries storage.
  // reinterpret_cast has no runtime cost.
  Value* inlineEntries() {
    ASSERT(inline_entries_ != nullptr);
    return reinterpret_cast<Value*>(inline_entries_.get());
  }

  // This is the reference of the descriptor that the inline map created by. This is used to
  // validate the inline handle validity and get the inline key set.
  const Descriptor& descriptor_;

  // This is the underlay hash map for the dynamic map entries.
  DynamicHashMap dynamic_entries_;
  // This is the memory that used to store the inline entries.
  StoragePtr inline_entries_;

  // This is the number of the valid inline entries.
  uint64_t inline_entries_size_{};

  // These are flags to indicate if the inline entries are valid. We keep pointer here to
  // avoid computing the offset every time.
  bool* inline_entries_valid_{};
};

} // namespace Envoy
