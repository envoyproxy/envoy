#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/stats/symbol_table_impl.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Stats {
namespace Fuzz {

// Fuzzer for symbol tables.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  FuzzedDataProvider provider(buf, len);
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);

  while (provider.remaining_bytes() != 0) {
    std::string next_data = provider.ConsumeRandomLengthString(provider.remaining_bytes());

    // ending with a "." is not considered legal, so just skip.
    if (!next_data.empty() && next_data[next_data.size() - 1] == '.') {
      continue;
    }

    StatName stat_name = pool.add(next_data);
    FUZZ_ASSERT(next_data == symbol_table.toString(stat_name));
  }
}

} // namespace Fuzz
} // namespace Stats
} // namespace Envoy
