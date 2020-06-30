#include <ostream>
#include <string>
#include <vector>

#include "common/stats/isolated_store_impl.h"
#include "common/stats/symbol_table_creator.h"
#include "common/stats/utility.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  {
    const absl::string_view string_buffer(reinterpret_cast<const char*>(buf), len);
    Stats::Utility::sanitizeStatsName(string_buffer);
  }
  if (len < 4)
    return;

  // Create a greater scope vector to store the string to prevent the string memory from being free
  std::vector<std::string> string_vector;
  auto make_string = [&string_vector](std::string str) -> absl::string_view {
    string_vector.push_back(std::string(str));
    return string_vector.back();
  };

  // generate a random number as the maximum length of the stat name
  const size_t max_len = *reinterpret_cast<const uint8_t*>(buf) % (len - 3);
  {
    FuzzedDataProvider provider(buf, len);

    // model common/stats/utility_test.cc, initialize those objects to create random elements as
    // input
    Stats::SymbolTablePtr symbol_table;
    if (len % 2 == 1)
      symbol_table = std::make_unique<Stats::FakeSymbolTableImpl>();
    else
      symbol_table = std::make_unique<Stats::SymbolTableImpl>();
    std::unique_ptr<Stats::IsolatedStoreImpl> store =
        std::make_unique<Stats::IsolatedStoreImpl>(*symbol_table);
    Stats::StatNamePool pool(*symbol_table);
    Stats::ScopePtr scope = store->createScope(provider.ConsumeRandomLengthString(max_len));
    Stats::ElementVec ele_vec;
    Stats::StatNameVec sn_vec;
    Stats::StatNameTagVector tags;
    Stats::StatName key, val;

    if (provider.remaining_bytes() == 0) {
      Stats::Utility::counterFromStatNames(*scope, {});
      Stats::Utility::counterFromElements(*scope, {});
    } else {
      // add random length string in each loop
      while (provider.remaining_bytes() > 3) {
        if (provider.ConsumeBool()) {
          if (provider.ConsumeBool()) {
            ele_vec.push_back(
                Stats::DynamicName(make_string(provider.ConsumeRandomLengthString(max_len))));
          }
          if (provider.ConsumeBool()) {
            sn_vec.push_back(pool.add(provider.ConsumeRandomLengthString(max_len)));
          }
        } else {
          key = pool.add(provider.ConsumeRandomLengthString(max_len));
          val = pool.add(provider.ConsumeRandomLengthString(max_len));
          tags.push_back({key, val});
        }
        Stats::Utility::counterFromStatNames(*scope, sn_vec, tags);
        Stats::Utility::counterFromElements(*scope, ele_vec, tags);
      }
    }
  }
}

} // namespace Fuzz
} // namespace Envoy