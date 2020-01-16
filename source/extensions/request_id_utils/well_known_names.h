#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace RequestIDUtils {

/**
 * Well-known request id utilities
 */
class RequestIDUtilNameValues {
public:
  // UUID based request ID tools
  const std::string UUID = "envoy.request_id_utils.uuid";
};

using RequestIDUtilNames = ConstSingleton<RequestIDUtilNameValues>;

} // namespace RequestIDUtils
} // namespace Extensions
} // namespace Envoy
