#pragma once

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

namespace TestUtil {
class SymbolTableCreatorTestPeer;
}

class SymbolTableCreator {
public:
  /**
   * Initializes the symbol-table creation system. Once this is called, it is a
   * runtime assertion to call this again in production code, changing the
   * use_fakes setting. However, tests can change the setting via
   * TestUtil::SymbolTableCreatorTestPeer::setUseFakeSymbolTables(use_fakes).
   *
   * @param use_fakes Whether to use fake symbol tables; typically from a command-line option.
   * @return a SymbolTable.
   */
  static SymbolTablePtr initAndMakeSymbolTable(bool use_fakes);

  /**
   * Factory method to create SymbolTables. This is needed to help make it
   * possible to flag-flip use of real symbol tables, and ultimately should be
   * removed.
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
