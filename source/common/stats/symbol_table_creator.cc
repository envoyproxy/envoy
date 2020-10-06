#include "common/stats/symbol_table_creator.h"

namespace Envoy {
namespace Stats {

SymbolTablePtr SymbolTableCreator::makeSymbolTable() { return std::make_unique<SymbolTableImpl>(); }

} // namespace Stats
} // namespace Envoy
