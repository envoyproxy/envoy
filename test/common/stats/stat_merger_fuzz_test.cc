#include <algorithm>

#include "source/common/stats/stat_merger.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Stats {
namespace Fuzz {

void testDynamicEncoding(absl::string_view data, SymbolTable& symbol_table) {
  if (data.empty()) {
    // Return early for empty strings.
    return;
  }
  StatNameDynamicPool dynamic_pool(symbol_table);
  StatNamePool symbolic_pool(symbol_table);
  StatNameVec stat_names;

  // This local string is write-only; it's used to help when debugging
  // a crash. If a crash is found, you can print the unit_test_encoding
  // in the debugger and then add that as a test-case in stat_merger_text.cc,
  // in StatMergerDynamicTest.DynamicsWithFakeSymbolTable and
  // StatMergerDynamicTest.DynamicsWithRealSymbolTable.
  std::string unit_test_encoding;

  for (uint32_t index = 0; index < data.size() - 1;) {
    if (index != 0) {
      unit_test_encoding += ".";
    }

    // Select component lengths between 1 and 8 bytes inclusive, and ensure it
    // doesn't overrun our buffer.
    //
    // TODO(#10008): We should remove the "1 +" below, so we can get empty
    // segments, which trigger some inconsistent handling as described in that
    // bug.
    uint32_t num_bytes = 1 + (data[index] & 0x7);

    // Carve out the segment and use the 4th bit from the control-byte to
    // determine whether to treat this segment symbolic or not.
    bool is_symbolic = (data[index] & 0x8) == 0x0;
    ++index;
    ASSERT(data.size() > index);
    uint32_t remaining = data.size() - index;
    num_bytes = std::min(remaining, num_bytes); // restrict number up to the size of data

    absl::string_view segment = data.substr(index, num_bytes);
    index += num_bytes;
    if (is_symbolic) {
      absl::string_view::size_type pos = segment.find_first_not_of('.');
      segment.remove_prefix((pos == absl::string_view::npos) ? segment.size() : pos);
      absl::StrAppend(&unit_test_encoding, segment);
      stat_names.push_back(symbolic_pool.add(segment));
    } else {
      absl::StrAppend(&unit_test_encoding, "D:", absl::StrReplaceAll(segment, {{".", ","}}));
      stat_names.push_back(dynamic_pool.add(segment));
    }
  }

  SymbolTable::StoragePtr joined = symbol_table.join(stat_names);
  StatName stat_name(joined.get());

  StatMerger::DynamicContext dynamic_context(symbol_table);
  std::string name = symbol_table.toString(stat_name);
  StatMerger::DynamicsMap dynamic_map;
  DynamicSpans spans = symbol_table.getDynamicSpans(stat_name);
  if (!spans.empty()) {
    dynamic_map[name] = spans;
  }
  StatName decoded = dynamic_context.makeDynamicStatName(name, dynamic_map);
  std::string decoded_str = symbol_table.toString(decoded);
  FUZZ_ASSERT(name == decoded_str);
  FUZZ_ASSERT(stat_name == decoded);
}

// Fuzzer for symbol tables.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  SymbolTableImpl symbol_table;

  absl::string_view data(reinterpret_cast<const char*>(buf), len);
  testDynamicEncoding(data, symbol_table);
}

} // namespace Fuzz
} // namespace Stats
} // namespace Envoy
