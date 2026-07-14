#include "test/extensions/dynamic_modules/util.h"

#include <cstdlib>

#include "source/common/stats/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

uint64_t failureCounter(Stats::Scope& scope, absl::string_view leaf,
                        absl::string_view config_name) {
  Stats::StatNameDynamicPool pool(scope.symbolTable());
  Stats::StatNameTagVector tags{{pool.add("config_name"), pool.add(config_name)}};
  return Stats::Utility::counterFromElements(
             scope, {Stats::DynamicName("dynamic_modules"), Stats::DynamicName(leaf)}, tags)
      .value();
}

std::string testSharedObjectPath(std::string name, std::string language) {
  return TestEnvironment::substitute(
             "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/") +
         language + "/lib" + name + ".so";
}

void DynamicModulesTestEnvironment::setModulesSearchPath() {
  std::string path =
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c");
  setenv("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", path.c_str(), 1);
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
