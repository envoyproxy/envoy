#include <string>
#include <vector>

#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/utility.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

namespace {

// The maximum number of iterations the fuzz test can run until stopped. This is
// to avoid lengthy tests and timeouts.
constexpr size_t MaxIterations = 1000;

} // namespace

DEFINE_FUZZER(const uint8_t* buf, size_t len) {

  Stats::Utility::sanitizeStatsName(absl::string_view(reinterpret_cast<const char*>(buf), len));

  if (len < 4) {
    return;
  }

  // Create a greater scope vector to store the string to prevent the string memory from being free
  std::list<std::string> string_list;
  auto make_string = [&string_list](absl::string_view str) -> absl::string_view {
    string_list.push_back(std::string(str));
    return string_list.back();
  };

  // generate a random number as the maximum length of the stat name
  const size_t max_len = *reinterpret_cast<const uint8_t*>(buf) % (len - 3);
  FuzzedDataProvider provider(buf, len);

  // model common/stats/utility_test.cc, initialize those objects to create random elements as
  // input
  Stats::SymbolTableImpl symbol_table;
  std::unique_ptr<Stats::IsolatedStoreImpl> store =
      std::make_unique<Stats::IsolatedStoreImpl>(symbol_table);
  Stats::StatNamePool pool(symbol_table);
  Stats::ScopeSharedPtr scope = store->createScope(provider.ConsumeRandomLengthString(max_len));
  Stats::ElementVec ele_vec;
  Stats::StatNameVec sn_vec;
  Stats::StatNameTagVector tags;
  Stats::StatName key, val;

  if (provider.remaining_bytes() == 0) {
    Stats::Utility::counterFromStatNames(*scope, {});
    Stats::Utility::counterFromElements(*scope, {});
  } else {
    // Run until either running out of strings to process or a maximal number of
    // iterations is reached.
    for (size_t iter = 0; iter < MaxIterations && provider.remaining_bytes() > 3; iter++) {
      // add random length string in each loop
      if (provider.ConsumeBool()) {
        absl::string_view str = make_string(
            provider.ConsumeRandomLengthString(std::min(max_len, provider.remaining_bytes())));
        ele_vec.push_back(Stats::DynamicName(str));
        sn_vec.push_back(pool.add(str));
      } else {
        key = pool.add(
            provider.ConsumeRandomLengthString(std::min(max_len, provider.remaining_bytes() / 2)));
        val = pool.add(
            provider.ConsumeRandomLengthString(std::min(max_len, provider.remaining_bytes())));
        tags.push_back({key, val});
      }
      Stats::Utility::counterFromStatNames(*scope, sn_vec, tags);
      Stats::Utility::counterFromElements(*scope, ele_vec, tags);
    }
  }
}

} // namespace Fuzz
} // namespace Envoy
