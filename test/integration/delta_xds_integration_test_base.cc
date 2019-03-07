#include "test/integration/delta_xds_integration_test_base.h"

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/stats/scope.h"

#include "common/config/resources.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::IsSubstring;

namespace Envoy {

void DeltaXdsIntegrationTestBase::createXdsConnection(FakeUpstream& upstream) {
  xds_upstream_ = &upstream;
  AssertionResult result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
}

void DeltaXdsIntegrationTestBase::cleanUpXdsConnection() {
  // Don't ASSERT fail if an xDS reconnect ends up unparented.
  if (xds_upstream_) {
    xds_upstream_->set_allow_unexpected_disconnects(true);
  }
  AssertionResult result = xds_connection_->close();
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());
  xds_connection_.reset();
}

AssertionResult DeltaXdsIntegrationTestBase::compareDiscoveryRequest(
    const std::string& expected_type_url,
    const std::vector<std::string>& expected_resource_subscriptions,
    const std::vector<std::string>& expected_resource_unsubscriptions,
    const Protobuf::int32 expected_error_code, const std::string& expected_error_message) {
  envoy::api::v2::DeltaDiscoveryRequest request;
  VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, request));

  EXPECT_TRUE(request.has_node());
  EXPECT_FALSE(request.node().id().empty());
  EXPECT_FALSE(request.node().cluster().empty());

  // TODO(PiotrSikora): Remove this hack once fixed internally.
  if (!(expected_type_url == request.type_url())) {
    return AssertionFailure() << fmt::format("type_url {} does not match expected {}",
                                             request.type_url(), expected_type_url);
  }
  if (!(expected_error_code == request.error_detail().code())) {
    return AssertionFailure() << fmt::format("error_code {} does not match expected {}",
                                             request.error_detail().code(), expected_error_code);
  }
  EXPECT_TRUE(IsSubstring("", "", expected_error_message, request.error_detail().message()));

  const std::vector<std::string> resource_subscriptions(request.resource_names_subscribe().cbegin(),
                                                        request.resource_names_subscribe().cend());
  if (expected_resource_subscriptions != resource_subscriptions) {
    return AssertionFailure() << fmt::format(
               "newly subscribed resources {} do not match expected {} in {}",
               fmt::join(resource_subscriptions.begin(), resource_subscriptions.end(), ","),
               fmt::join(expected_resource_subscriptions.begin(),
                         expected_resource_subscriptions.end(), ","),
               request.DebugString());
  }
  const std::vector<std::string> resource_unsubscriptions(
      request.resource_names_unsubscribe().cbegin(), request.resource_names_unsubscribe().cend());
  if (expected_resource_unsubscriptions != resource_unsubscriptions) {
    return AssertionFailure() << fmt::format(
               "newly UNsubscribed resources {} do not match expected {} in {}",
               fmt::join(resource_unsubscriptions.begin(), resource_unsubscriptions.end(), ","),
               fmt::join(expected_resource_unsubscriptions.begin(),
                         expected_resource_unsubscriptions.end(), ","),
               request.DebugString());
  }
  return AssertionSuccess();
}

} // namespace Envoy
