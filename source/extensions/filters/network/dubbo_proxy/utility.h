#include <string>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class Utility {
public:
  static bool isContainWildcard(const std::string& input);
  static bool wildcardMatch(const char* input, const char* pattern);
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
