#include "common/stats/symbol_table_creator.h"

namespace Envoy {
namespace Stats {

bool SymbolTableCreator::initialized_ = false;
bool SymbolTableCreator::use_fake_symbol_tables_ = false;

SymbolTablePtr SymbolTableCreator::initAndMakeSymbolTable(bool use_fake) {
  ASSERT(!initialized_ || (use_fake_symbol_tables_ == use_fake));
  use_fake_symbol_tables_ = use_fake;
  return makeSymbolTable();
}

SymbolTablePtr SymbolTableCreator::makeSymbolTable() {
  initialized_ = true;
  if (use_fake_symbol_tables_) {
    return std::make_unique<FakeSymbolTableImpl>();
  }
  return std::make_unique<SymbolTableImpl>();
}

} // namespace Stats
} // namespace Envoy
