#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "extensions/filters/network/common/redis/codec_impl.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

/**
 * Pretty print const RespValue& value
 */

void PrintTo(const RespValue& value, std::ostream* os);
void PrintTo(const RespValuePtr& value, std::ostream* os);
bool operator==(const RespValue& lhs, const RespValue& rhs);

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
