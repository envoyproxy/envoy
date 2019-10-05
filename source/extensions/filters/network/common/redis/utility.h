#pragma once

#include <string>

#include "extensions/filters/network/common/redis/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Utility {

Redis::RespValue makeAuthCommand(const std::string& password);

class ReadOnlyRequest : public Redis::RespValue {
public:
  ReadOnlyRequest();
  static const ReadOnlyRequest& instance();
};

} // namespace Utility
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
