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
  // SymbolTable
  void populateList(absl::string_view* names, int32_t num_names, StatNameList& list) override {
    // This implementation of populateList is similar to
    // SymboLableImpl::populateList. This variant is more efficient for
    // FakeSymbolTableImpl, because it avoid "encoding" each name in names. The
    // strings are laid out abutting each other with 2-byte length prefixes, so
    // encoding isn't needed, and doing a dummy encoding step would cost one
    // memory allocation per element, adding significant overhead as measured by
    // thread_local_store_speed_test.

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

  void callWithStringView(StatName stat_name,
                          const std::function<void(absl::string_view)>& fn) const override {
    fn(toStringView(stat_name));
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
    auto storage = std::make_unique<Storage>(name.size() + 2);
    uint8_t* p = saveLengthToBytesReturningNext(name.size(), storage.get());
    memcpy(p, name.data(), name.size());
    return storage;
  }
};

} // namespace Stats
} // namespace Envoy
