#import "library/cc/bridge_utility.h"
#import "library/cc/direct_response_testing.h"
#import "library/cc/engine_builder.h"
#import "library/common/data/utility.h"
#import "library/common/extensions/filters/http/platform_bridge/c_types.h"
#import "library/common/main_interface.h"
#import "library/common/network/apple_platform_cert_verifier.h"
#import "library/common/stats/utility.h"

namespace Envoy {
namespace CxxSwift {

using StringVector = std::vector<std::string>;
using StringPair = std::pair<std::string, std::string>;
using StringPairVector = std::vector<StringPair>;
using StringMap = absl::flat_hash_map<std::string, std::string>;
using HeaderMatcherVector = std::vector<DirectResponseTesting::HeaderMatcher>;
using BootstrapPtr = intptr_t;

inline void string_map_set(StringMap& map, std::string key, std::string value) { map[key] = value; }

inline void raw_header_map_set(Platform::RawHeaderMap& map, std::string key,
                               std::vector<std::string> value) {
  map[key] = value;
}

inline const DirectResponseTesting::MatchMode DirectResponseMatchModeContains =
    DirectResponseTesting::contains;
inline const DirectResponseTesting::MatchMode DirectResponseMatchModeExact =
    DirectResponseTesting::exact;
inline const DirectResponseTesting::MatchMode DirectResponseMatchModePrefix =
    DirectResponseTesting::prefix;
inline const DirectResponseTesting::MatchMode DirectResponseMatchModeSuffix =
    DirectResponseTesting::suffix;

inline BootstrapPtr generateBootstrapPtr(Platform::EngineBuilder builder) {
  return reinterpret_cast<BootstrapPtr>(builder.generateBootstrap().release());
}

void run(BootstrapPtr bootstrap_ptr, std::string log_level, envoy_engine_t engine_handle);

} // namespace CxxSwift
} // namespace Envoy
