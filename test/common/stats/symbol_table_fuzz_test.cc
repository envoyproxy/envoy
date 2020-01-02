#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/stats/symbol_table_impl.h"

#include "test/common/stats/stat_test_utility.h"
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
    StatName stat_name = pool.add(next_data);

    // We can add stat-names with trailing dots, but note that they will be
    // trimmed by the Symbol Table implementation, so we must trim the input
    // string before comparing.
    absl::string_view trimmed_fuzz_data = StringUtil::removeTrailingCharacters(next_data, '.');
    FUZZ_ASSERT(trimmed_fuzz_data == symbol_table.toString(stat_name));

    // Also encode the string directly, without symbolizing it.
    TestUtil::serializeDeserializeString(next_data);

    // Grab the first few bytes from next_data to synthesize together a random uint64_t.
    if (next_data.size() > 1) {
      uint32_t num_bytes = (next_data[0] % 8) + 1; // random number between 1 and 8 inclusive.
      num_bytes = std::min(static_cast<uint32_t>(next_data.size()),
                           num_bytes); // restrict number up to the size of next_data
      uint64_t number = 0;
      for (uint32_t i = 0; i < num_bytes; ++i) {
        number = 256 * number + next_data[i + 1];
      }
      TestUtil::serializeDeserializeNumber(number);
    }
  }
}

} // namespace Fuzz
} // namespace Stats
} // namespace Envoy
