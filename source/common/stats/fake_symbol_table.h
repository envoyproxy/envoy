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
 * See the documentation for SymbolTable in symbol_table_impl.h. This is a fake
 * implementation of that, which does not actually keep any tables or map,
 * intended only for transitioning the stats codebase to the SymbolTable API
 * without introducing any computational change.
 */
class FakeSymbolTable : public SymbolTable {
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
  SymbolStoragePtr join(const StatName& a, const StatName& b) const override {
    return join({a, b});
  }
  SymbolStoragePtr join(const std::vector<StatName>& names) const override {
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
    for (char c : name) {
      encoding.addSymbol(static_cast<Symbol>(c));
    }
    return encoding;
  }

  absl::string_view toStringView(const StatName& stat_name) const {
    return absl::string_view(reinterpret_cast<const char*>(stat_name.data()), stat_name.dataSize());
  }

  SymbolStoragePtr stringToStorage(absl::string_view name) const {
    SymbolEncoding encoding = encodeHelper(name);
    auto bytes = std::make_unique<uint8_t[]>(encoding.bytesRequired());
    encoding.moveToStorage(bytes.get());
    return bytes;
  }
};

} // namespace Stats
} // namespace Envoy
