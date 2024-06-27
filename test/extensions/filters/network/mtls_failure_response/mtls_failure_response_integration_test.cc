#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.h"
#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.validate.h"

#include "source/extensions/filters/network/mtls_failure_response/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MtlsFailureResponse {

namespace {

  // Test to create a filter closing connections
  // Assert that no token bucket is created

  // Test to create a filter keeping connections open without any limit

  // Test to create a filter keeping connections open with a token bucket and check it is getting closed

  // Test to not fail on this filter


} // namespace MtlsFailureResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
