#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/stats/symbol_table_impl.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {
namespace Fuzz {

// Adds a stat-name to the symbol table, discarding it if it ends in ".", and
// splitting it in two if it's too long (64k bytes).
//
// The actual requirement for StatName allows up to 64k "."-separated segments
// of 64k, but it's simpler to just limit the whole string. I don't think
// there's that much value in using strings larger than 64k.
void addSymbol(StatNamePool& pool, SymbolTable& symbol_table, absl::string_view str) {
  while (absl::EndsWith(str, ".")) {
    str.remove_suffix(1);
  }

  if (str.size() >= StatNameMaxSize) {
    size_t halfway = str.size() / 2;
    addSymbol(pool, symbol_table, str.substr(0, halfway));
    addSymbol(pool, symbol_table, str.substr(halfway));
  } else if (!str.empty()) {
    StatName stat_name = pool.add(str);
    FUZZ_ASSERT(str == symbol_table.toString(stat_name));
  }
}

// Fuzzer for symbol tables.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  FuzzedDataProvider provider(buf, len);
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);

  while (provider.remaining_bytes() != 0) {
    addSymbol(pool, symbol_table, provider.ConsumeRandomLengthString(provider.remaining_bytes()));
  }
}

} // namespace Fuzz
} // namespace Stats
} // namespace Envoy
