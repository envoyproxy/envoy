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
    StatName stat_name = pool.add(next_data);

    // We can add stat-names with trailing dots, but note that they will be
    // trimmed by the Symbol Table implementation, so we must trim the input
    // string before comparing.
    absl::string_view trimmed_fuzz_data = StringUtil::removeTrailingCharacters(next_data, '.');
    FUZZ_ASSERT(trimmed_fuzz_data == symbol_table.toString(stat_name));

    // Also encode the string directly, without symbolizing it.
    MemBlockBuilder<uint8_t> mem_block(SymbolTableImpl::Encoding::totalSizeBytes(next_data.size()));
    SymbolTableImpl::Encoding::appendEncoding(next_data.size(), mem_block);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(next_data.data());
    mem_block.appendData(absl::MakeSpan(data, data + next_data.size()));
    FUZZ_ASSERT(mem_block.capacityRemaining() == 0);
    absl::Span<uint8_t> span = mem_block.span();
    std::pair<uint64_t, uint64_t> number_consumed =
        SymbolTableImpl::Encoding::decodeNumber(span.data());
    FUZZ_ASSERT(number_consumed.first == next_data.size());
    span.remove_prefix(number_consumed.second);
    FUZZ_ASSERT(absl::string_view(reinterpret_cast<const char*>(span.data()), span.size()) ==
                next_data);
  }
}

} // namespace Fuzz
} // namespace Stats
} // namespace Envoy
