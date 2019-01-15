#pragma once

#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Aws {

class Utility {
public:
  /**
   * Creates a canonicalized header map according to
   * https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param headers a header map to canonicalize.
   * @return a map of header key value pairs used for signing requests.
   */
  static std::map<std::string, std::string> canonicalizeHeaders(const Http::HeaderMap& headers);
};

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy