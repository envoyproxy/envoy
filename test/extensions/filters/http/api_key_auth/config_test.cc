#include "envoy/extensions/filters/http/basic_auth/v3/basic_auth.pb.h"

#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"
#include "source/extensions/filters/http/basic_auth/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ApiKeyAuth {} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
