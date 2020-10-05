#include "common/stats/symbol_table_creator.h"

namespace Envoy {
namespace Stats {

SymbolTablePtr SymbolTableCreator::initAndMakeSymbolTable(bool use_fake) {
  if (use_fake) {
    ENVOY_LOG_MISC(warn, "Fake symbol tables have been removed. Please remove references to "
                         "--use-fake-symbol-table");
  }
  return makeSymbolTable();
}

SymbolTablePtr SymbolTableCreator::makeSymbolTable() { return std::make_unique<SymbolTableImpl>(); }

} // namespace Stats
} // namespace Envoy
