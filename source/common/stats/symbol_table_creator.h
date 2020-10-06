#pragma once

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

class SymbolTableCreator {
public:
  /**
   * Factory method to create SymbolTables. This was originally needed to help
   * make it possible to flag-flip use of real symbol tables.
   *
   * TODO(jmarantz): replace this method and calls to it with direct
   * instantiations of SymbolTableImpl.
   *
   * @return a SymbolTable.
   */
  static SymbolTablePtr makeSymbolTable();

  /**
   * @return whether the system is initialized to use fake symbol tables.
   */
  static bool useFakeSymbolTables() { return false; }
};

} // namespace Stats
} // namespace Envoy
