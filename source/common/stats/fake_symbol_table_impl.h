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
 * TODO(#6307): delete this class once SymbolTable is fully deployed in the
 * Envoy codebase.
 */
class FakeSymbolTableImpl : public SymbolTable {
public:
  // SymbolTable
  void populateList(const StatName* names, uint32_t num_names, StatNameList& list) override {
    // This implementation of populateList is similar to
    // SymbolTableImpl::populateList. This variant is more efficient for
    // FakeSymbolTableImpl, because it avoid "encoding" each name in names. The
    // strings are laid out abutting each other with 2-byte length prefixes, so
    // encoding isn't needed, and doing a dummy encoding step would cost one
    // memory allocation per element, adding significant overhead as measured by
    // thread_local_store_speed_test.

    // We encode the number of names in a single byte, thus there must be less
    // than 256 of them.
    RELEASE_ASSERT(num_names < 256, "Maximum number elements in a StatNameList exceeded");

    size_t total_size_bytes = 1; /* one byte for holding the number of names */
    for (uint32_t i = 0; i < num_names; ++i) {
      total_size_bytes += names[i].size();
    }

    // Now allocate the exact number of bytes required and move the encodings
    // into storage.
    MemBlockBuilder<uint8_t> mem_block(total_size_bytes);
    mem_block.appendOne(num_names);
    for (uint32_t i = 0; i < num_names; ++i) {
      SymbolTableImpl::Encoding::appendToMemBlock(names[i], mem_block);
    }

    // This assertion double-checks the arithmetic where we computed
    // total_size_bytes. After appending all the encoded data into the
    // allocated byte array, we should have exhausted all the memory
    // we though we needed.
    ASSERT(mem_block.capacityRemaining() == 0);
    list.moveStorageIntoList(mem_block.release());
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
  StoragePtr encode(absl::string_view name) override { return encodeHelper(name); }
  StoragePtr makeDynamicStorage(absl::string_view name) override { return encodeHelper(name); }
  SymbolTable::StoragePtr join(const StatNameVec& names) const override {
    std::vector<absl::string_view> strings;
    for (StatName name : names) {
      if (!name.empty()) {
        strings.push_back(toStringView(name));
      }
    }
    return encodeHelper(absl::StrJoin(strings, "."));
  }

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint() const override {}
#endif

  void callWithStringView(StatName stat_name,
                          const std::function<void(absl::string_view)>& fn) const override {
    fn(toStringView(stat_name));
  }

  StatNameSetPtr makeSet(absl::string_view name) override {
    // make_unique does not work with private ctor, even though FakeSymbolTableImpl is a friend.
    return StatNameSetPtr(new StatNameSet(*this, name));
  }
  uint64_t getRecentLookups(const RecentLookupsFn&) const override { return 0; }
  void clearRecentLookups() override {}
  void setRecentLookupCapacity(uint64_t) override {}
  uint64_t recentLookupCapacity() const override { return 0; }
  DynamicSpans getDynamicSpans(StatName) const override { return DynamicSpans(); }

private:
  absl::string_view toStringView(const StatName& stat_name) const {
    return {reinterpret_cast<const char*>(stat_name.data()),
            static_cast<absl::string_view::size_type>(stat_name.dataSize())};
  }

  StoragePtr encodeHelper(absl::string_view name) const {
    name = StringUtil::removeTrailingCharacters(name, '.');
    MemBlockBuilder<uint8_t> mem_block(SymbolTableImpl::Encoding::totalSizeBytes(name.size()));
    SymbolTableImpl::Encoding::appendEncoding(name.size(), mem_block);
    mem_block.appendData(
        absl::MakeSpan(reinterpret_cast<const uint8_t*>(name.data()), name.size()));
    ASSERT(mem_block.capacityRemaining() == 0);
    return mem_block.release();
  }
};

} // namespace Stats
} // namespace Envoy
