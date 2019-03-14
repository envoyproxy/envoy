#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/lock_guard.h"
#include "common/common/non_copyable.h"
#include "common/common/thread.h"
#include "common/common/utility.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Stats {

/**
 * Implements the SymbolTable interface without taking locks or saving memory.
 * This implementation is intended as a transient state for the Envoy codebase
 * to allow incremental conversion of Envoy stats call-sites to use the
 * SymbolTable interface, pre-allocating symbols during construction time for
 * all stats tokens.
 *
 * Once all stat tokens are symbolized at construction time, this
 * FakeSymbolTable implementation can be deleted, and real-symbol tables can be
 * used, thereby reducing memory and improving stat construction time.
 *
 * Note that it is not necessary to pre-allocate all elaborated stat names
 * because multiple StatNames can be joined together without taking locks,
 * even in SymbolTableImpl.
 *
 * This implementation simply stores the characters directly in the uint8_t[]
 * that backs each StatName, so there is no sharing or memory savings, but also
 * no state associated with the SymbolTable, and thus no locks needed.
 *
 * TODO(jmarantz): delete this class once SymbolTable is fully deployed in the
 * Envoy codebase.
 */
class FakeSymbolTableImpl : public SymbolTable {
public:
  /**
   * Represents an 8-bit encoding of a vector of symbols, used as a transient
   * representation during encoding and prior to retained allocation.
   */
  class Encoding {
   public:
    //Encoding() : storage_(nullptr) {}

    void fromString(absl::string_view str) {
      storage_ = std::make_unique<Storage>(str.size() + 2);
      uint8_t* p = saveLengthToBytesReturningNext(str.size(), storage_.get());
      memcpy(p, str.data(), str.size());
    }

    /*Encoding& operator=(Encoding&& src) {
      storage_ = std::move(src.storage_);
      return *this;
      }*/

    /*void swap(Encoding& rhs) {
      std::swap(rhs.storage_, storage_);
      }*/

    /**
     * Before destructing SymbolEncoding, you must call moveToStorage. This
     * transfers ownership, and in particular, the responsibility to call
     * SymbolTable::clear() on all referenced symbols. If we ever wanted
     * to be able to destruct a SymbolEncoding without transferring it
     * we could add a clear(SymbolTable&) method.
     */
    ~Encoding() { ASSERT(storage_ == nullptr); }

    uint64_t bytesRequired() const { return StatName(storage_.get()).size(); }

    /**
     * Moves the contents of the vector into an allocated array. The array
     * must have been allocated with bytesRequired() bytes.
     *
     * @param array destination memory to receive the encoded bytes.
     * @return uint64_t the number of bytes transferred.
     */
    uint64_t moveToStorage(SymbolTable::Storage array) {
      uint64_t bytes_required = bytesRequired();
      memcpy(array, storage_.get(), bytes_required);
      storage_.reset();
      return bytes_required;
    }

    StoragePtr transferStorage() {
      return std::move(storage_);
    }

   private:
    StoragePtr storage_;
  };

  void encode(absl::string_view name, Encoding& encoding) { encoding.fromString(name); }

  void populateList(absl::string_view* names, int32_t num_names, StatNameList& list) override {
    RELEASE_ASSERT(num_names < 256, "Maximum number elements in a StatNameList exceeded");

    // First encode all the names.
    size_t total_size_bytes = 1 + num_names * StatNameSizeEncodingBytes;

    for (int32_t i = 0; i < num_names; ++i) {
      total_size_bytes += names[i].size();
    }

    // Now allocate the exact number of bytes required and move the encodings
    // into storage.
    auto storage = std::make_unique<uint8_t[]>(total_size_bytes);
    uint8_t* p = &storage[0];
    *p++ = num_names;
    for (int32_t i = 0; i < num_names; ++i) {
      auto& name = names[i];
      size_t sz = name.size();
      p = saveLengthToBytesReturningNext(sz, p);
      if (!name.empty()) {
        memcpy(p, name.data(), sz * sizeof(uint8_t));
        p += sz;
      }
    }
    ASSERT(p == &storage[0] + total_size_bytes);
    list.moveStorageIntoList(std::move(storage));
  }

  std::string toString(const StatName& stat_name) const override {
    return std::string(toStringView(stat_name));
  }
  uint64_t numSymbols() const override { return 0; }
  bool lessThan(const StatName& a, const StatName& b) const override {
    return toStringView(a) < toStringView(b);
  }
  void free(const StatName&) override {}
  void incRefCount(const StatName&) override {}
  SymbolTable::StoragePtr join(const std::vector<StatName>& names) const override {
    std::vector<absl::string_view> strings;
    for (StatName name : names) {
      absl::string_view str = toStringView(name);
      if (!str.empty()) {
        strings.push_back(str);
      }
    }
    return stringToStorage(absl::StrJoin(strings, "."));
  }

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint() const override {}
#endif

  StoragePtr copyToBytes(absl::string_view name) override {
    auto bytes = std::make_unique<uint8_t[]>(name.size() + StatNameSizeEncodingBytes);
    uint8_t* buffer = saveLengthToBytesReturningNext(name.size(), bytes.get());
    memcpy(buffer, name.data(), name.size());
    return bytes;
  }

private:
  // Saves the specified length into the byte array, returning the next byte.
  // There is no guarantee that bytes will be aligned, so we can't cast to a
  // uint16_t* and assign, but must individually copy the bytes.
  static uint8_t* saveLengthToBytesReturningNext(uint64_t length, uint8_t* bytes) {
    ASSERT(length < StatNameMaxSize);
    *bytes++ = length & 0xff;
    *bytes++ = length >> 8;
    return bytes;
  }

  absl::string_view toStringView(const StatName& stat_name) const {
    return {reinterpret_cast<const char*>(stat_name.data()), stat_name.dataSize()};
  }

  StoragePtr stringToStorage(absl::string_view name) const {
    Encoding encoding;
    encoding.fromString(name);
    return encoding.transferStorage();
  }
};

} // namespace Stats
} // namespace Envoy
