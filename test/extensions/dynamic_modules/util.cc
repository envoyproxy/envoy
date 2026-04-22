#include "test/extensions/dynamic_modules/util.h"

#include <cstdlib>

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

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
