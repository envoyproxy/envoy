#include "test/integration/xds_integration_test_base.h"

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

void XdsIntegrationTestBase::createXdsConnection(FakeUpstream& upstream) {
  xds_upstream_ = &upstream;
  AssertionResult result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
}

void XdsIntegrationTestBase::cleanUpXdsConnection() {
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

AssertionResult XdsIntegrationTestBase::compareDiscoveryRequest(
    const std::string& expected_type_url, const std::string& expected_version,
    const std::vector<std::string>& expected_resource_names,
    const Protobuf::int32 expected_error_code, const std::string& expected_error_message) {
  envoy::api::v2::DiscoveryRequest discovery_request;
  VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request));

  EXPECT_TRUE(discovery_request.has_node());
  EXPECT_FALSE(discovery_request.node().id().empty());
  EXPECT_FALSE(discovery_request.node().cluster().empty());

  // TODO(PiotrSikora): Remove this hack once fixed internally.
  if (!(expected_type_url == discovery_request.type_url())) {
    return AssertionFailure() << fmt::format("type_url {} does not match expected {}",
                                             discovery_request.type_url(), expected_type_url);
  }
  if (!(expected_error_code == discovery_request.error_detail().code())) {
    return AssertionFailure() << fmt::format("error_code {} does not match expected {}",
                                             discovery_request.error_detail().code(),
                                             expected_error_code);
  }
  EXPECT_TRUE(
      IsSubstring("", "", expected_error_message, discovery_request.error_detail().message()));
  const std::vector<std::string> resource_names(discovery_request.resource_names().cbegin(),
                                                discovery_request.resource_names().cend());
  if (expected_resource_names != resource_names) {
    return AssertionFailure() << fmt::format(
               "resources {} do not match expected {} in {}",
               fmt::join(resource_names.begin(), resource_names.end(), ","),
               fmt::join(expected_resource_names.begin(), expected_resource_names.end(), ","),
               discovery_request.DebugString());
  }
  // TODO(PiotrSikora): Remove this hack once fixed internally.
  if (!(expected_version == discovery_request.version_info())) {
    return AssertionFailure() << fmt::format("version {} does not match expected {} in {}",
                                             discovery_request.version_info(), expected_version,
                                             discovery_request.DebugString());
  }
  return AssertionSuccess();
}

} // namespace Envoy
