#include "extensions/retry/header/response_headers/response_headers.h"

#include "envoy/router/router.h"

#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Header {

bool ResponseHeadersRetryHeader::shouldRetryHeader(const Http::HeaderMap&,
                                                   const Http::HeaderMap& response_headers) {
  if (Http::Utility::getResponseStatus(response_headers) >= 500) {
    return true;
  }

  return false;
}

} // namespace Header
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
