#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/stats/symbol_table_impl.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Stats {
namespace Fuzz {

// Fuzzer for symbol tables.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  FuzzedDataProvider provider(buf, len);
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  bool provider_empty = provider.remaining_bytes() == 0;

  const uint64_t ResetPoolAfterN = 10000;
  uint64_t num_in_pool = 0;

  while (!provider_empty) {
    if (num_in_pool == ResetPoolAfterN) {
      pool.clear();
      num_in_pool = 1;
      ENVOY_LOG_MISC(error, "Note: Resetting pool");
    } else {
      ++num_in_pool;
    }

    std::string next_data = provider.ConsumeRandomLengthString(provider.remaining_bytes());
    provider_empty = provider.remaining_bytes() == 0;
    if (!next_data.empty() && (next_data[next_data.size() - 1] == '.')) {
      next_data += "x"; // ending with a "." is not legal.
    }
    ENVOY_LOG_MISC(debug, "Processing {}", next_data);
    StatName stat_name = pool.add(next_data);
    RELEASE_ASSERT(next_data == symbol_table.toString(stat_name), "inequality for fuzz test");
  }
}

} // namespace Fuzz
} // namespace Stats
} // namespace Envoy
