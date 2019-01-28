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
  SymbolEncoding encode(absl::string_view name) override { return encodeHelper(name); }

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

private:
  SymbolEncoding encodeHelper(absl::string_view name) const {
    SymbolEncoding encoding;
    encoding.addStringForFakeSymbolTable(name);
    return encoding;
  }

  absl::string_view toStringView(const StatName& stat_name) const {
    return {reinterpret_cast<const char*>(stat_name.data()), stat_name.dataSize()};
  }

  SymbolTable::StoragePtr stringToStorage(absl::string_view name) const {
    SymbolEncoding encoding = encodeHelper(name);
    auto bytes = std::make_unique<uint8_t[]>(encoding.bytesRequired());
    encoding.moveToStorage(bytes.get());
    return bytes;
  }
};

} // namespace Stats
} // namespace Envoy
