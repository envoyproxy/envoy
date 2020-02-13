#pragma once

#include "envoy/stats/symbol_table.h"
#include "envoy/stats/tag.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {
SymbolTable::StoragePtr addTagSuffix(StatName name, const StatNameTagVector& tags,
                                     SymbolTable& symbol_table);
} // namespace TagUtility
} // namespace Stats
} // namespace Envoy