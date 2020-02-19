#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Header {

/**
 * Well-known retry header names.
 */
class RetryHeaderNameValues {
public:
  const std::string ResponseHeadersRetryHeader = "envoy.retry_header.response_headers";
};

using RetryHeaderValues = ConstSingleton<RetryHeaderNameValues>;

} // namespace Header
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
