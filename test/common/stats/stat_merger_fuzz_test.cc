#include <algorithm>

#include "common/stats/stat_merger.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Stats {
namespace Fuzz {

void testDynamicEncoding(absl::string_view data, SymbolTable& symbol_table) {
  StatNameDynamicPool dynamic_pool(symbol_table);
  StatNamePool symbolic_pool(symbol_table);
  std::vector<StatName> stat_names;

  // This local string is write-only; it's used to help when debugging
  // a crash. If a crash is found, you can print the unit_test_encoding
  // in the debugger and then add that as a test-case in stat_merger_text.cc,
  // in StatMergerDynamicTest.DynamicsWithFakeSymbolTable and
  // StatMergerDynamicTest.DynamicsWithRealSymbolTable.
  std::string unit_test_encoding;

  for (uint32_t index = 0; index < data.size();) {
    // Select component lengths between 1 and 8 bytes inclusive, and ensure it
    // doesn't overrun our buffer.
    //
    // TODO(#10008): We should remove the "1 +" below, so we can get empty
    // segments, which trigger some inconsistent handling as described in that
    // bug.
    uint32_t num_bytes = 1 + data[index] & 0x7;
    num_bytes = std::min(static_cast<uint32_t>(data.size() - 1),
                         num_bytes); // restrict number up to the size of data

    // Carve out the segment and use the 4th bit from the control-byte to
    // determine whether to treat this segment symbolic or not.
    absl::string_view segment = data.substr(index, num_bytes);
    bool is_symbolic = (data[index] & 0x8) == 0x0;
    if (index != 0) {
      unit_test_encoding += ".";
    }
    index += num_bytes + 1;
    if (is_symbolic) {
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
  dynamic_map[name] = symbol_table.getDynamicSpans(stat_name);

  StatName decoded = dynamic_context.makeDynamicStatName(name, dynamic_map);
  FUZZ_ASSERT(name == symbol_table.toString(decoded));
  FUZZ_ASSERT(stat_name == decoded);
}

// Fuzzer for symbol tables.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  FakeSymbolTableImpl fake_symbol_table;
  SymbolTableImpl symbol_table;

  absl::string_view data(reinterpret_cast<const char*>(buf), len);
  testDynamicEncoding(data, fake_symbol_table);
  testDynamicEncoding(data, symbol_table);
}

} // namespace Fuzz
} // namespace Stats
} // namespace Envoy
