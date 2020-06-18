#include <string>

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

  // generate a random number as the maximum length of the stat name
  const size_t max_len = *reinterpret_cast<const uint8_t*>(buf) % len;
  {
    FuzzedDataProvider provider(buf, len);

    // model common/stats/utility_test.cc, initilize those objects to create random elements as input
    Stats::SymbolTablePtr symbol_table = Stats::SymbolTableCreator::makeSymbolTable();
    std::unique_ptr<Stats::IsolatedStoreImpl> store =
        std::make_unique<Stats::IsolatedStoreImpl>(*symbol_table);
    Stats::StatNamePool pool(*symbol_table);
    Stats::ScopePtr scope = store->createScope(provider.ConsumeRandomLengthString(max_len));
    Stats::ElementVec ele_vec;
    Stats::StatNameVec sn_vec;
    Stats::StatNameTagVector tags;

    if (provider.remaining_bytes() == 0) {
      Stats::Utility::counterFromStatNames(*scope, {});
      Stats::Utility::counterFromElements(*scope, {});
    } else {
      // add ranndom length string in each loop
      while (provider.remaining_bytes() > 0) {
        size_t random = provider.ConsumeIntegral<uint8_t>();
        if (random % 2 == 1) {
          std::string str = provider.ConsumeRandomLengthString(max_len);
          ele_vec.push_back(Stats::DynamicName(str));
          Stats::StatName sn = pool.add(str);
          sn_vec.push_back(sn);
        } else {
          tags.push_back({pool.add(provider.ConsumeRandomLengthString(max_len)),
                          pool.add(provider.ConsumeRandomLengthString(max_len))});
        }
        Stats::Utility::counterFromStatNames(*scope, sn_vec, tags);
        Stats::Utility::counterFromElements(*scope, ele_vec, tags);
      }
    }
  }
}

} // namespace Fuzz
} // namespace Envoy
