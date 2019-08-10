#pragma once

#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

class SymbolTableCreator {
public:
  // Factory method to create SymbolTables. This is needed to help make it
  // possible to flag-flip use of real symbol tables, and but ultimately
  // should be removed.
  static SymbolTablePtr makeSymbolTable(bool use_fake) {
    use_fake_symbol_tables_ = use_fake;
    return makeSymbolTable();
  }

  static SymbolTablePtr makeSymbolTable() {
    if (use_fake_symbol_tables_) {
      return std::make_unique<FakeSymbolTableImpl>();
    }
    return std::make_unique<SymbolTableImpl>();
  }

  // Sets whether fake or real symbol tables should be used. Tests that alter
  // this should restore previous value at the end of the test.
  static void setUseFakeSymbolTables(bool use_fakes) { use_fake_symbol_tables_ = use_fakes; }
  static bool useFakeSymbolTables() { return use_fake_symbol_tables_; }

private:
  static bool use_fake_symbol_tables_;
};

} // namespace Stats
} // namespace Envoy
