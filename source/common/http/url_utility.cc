#include "common/http/url_utility.h"

namespace Envoy {
namespace Http {
namespace Utility {
bool GoogleUrl::test() {
  GURL parsed("http://ok.com");
  return parsed.is_valid();
}
} // namespace Utility
} // namespace Http
} // namespace Envoy
