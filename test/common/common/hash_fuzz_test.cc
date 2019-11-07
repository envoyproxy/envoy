#include "common/common/hash.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {
namespace {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const std::string input(reinterpret_cast<const char*>(buf), len);
  { HashUtil::xxHash64(input); }
  { HashUtil::djb2CaseInsensitiveHash(input); }
  { MurmurHash::murmurHash2_64(input); }
  if (len > 0) {
    // Split the input string into two parts to make a key-value pair.
    const size_t split_point = *reinterpret_cast<const uint8_t*>(buf) % len;
    const std::string key = input.substr(0, split_point);
    const std::string value = input.substr(split_point);
    StringMap<std::string> map;
    map[key] = value;
    map.find(key);
  }
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
