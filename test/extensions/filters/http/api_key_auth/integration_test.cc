#include <string>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/basic_auth/v3/basic_auth.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ApiKeyAuth {
namespace {} // namespace
} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
