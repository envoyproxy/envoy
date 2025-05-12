#include "test/extensions/dynamic_modules/util.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

std::string testSharedObjectPath(std::string name, std::string language) {
  return TestEnvironment::substitute(
             "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/") +
         language + "/lib" + name + ".so";
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
