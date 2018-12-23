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
  SymbolEncoding encode(absl::string_view name) override {
    SymbolEncoding encoding;
    encoding.vec_.resize(name.size());
    memcpy(encoding.vec_.data(), name.data(), name.size());
    return encoding;
  }

  std::string toString(const StatName& stat_name) const override {
    return std::string(reinterpret_cast<const char*>(stat_name.data()), stat_name.dataSize());
  }

  uint64_t numSymbols() const override { return 0; }
  bool lessThan(const StatName& a, const StatName& b) const override {
    return toString(a) < toString(b);
  }
  void free(const StatName&) {}
  void incRefCount(const StatName&) {}
};

} // namespace Stats
} // namespace Envoy
