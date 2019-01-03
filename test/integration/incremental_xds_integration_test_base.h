#pragma once

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

class IncrementalXdsIntegrationTestBase : public HttpIntegrationTest {
public:
  IncrementalXdsIntegrationTestBase(Http::CodecClient::Type downstream_protocol,
                                    Network::Address::IpVersion version)
      : HttpIntegrationTest(downstream_protocol, version, realTime()) {}
  IncrementalXdsIntegrationTestBase(Http::CodecClient::Type downstream_protocol,
                                    Network::Address::IpVersion version, const std::string& config)
      : HttpIntegrationTest(downstream_protocol, version, realTime(), config) {}

  void createXdsConnection(FakeUpstream& upstream);

  void cleanUpXdsConnection();

  AssertionResult
  compareDiscoveryRequest(const std::string& expected_type_url,
                          const std::vector<std::string>& expected_resource_subscriptions,
                          const std::vector<std::string>& expected_resource_unsubscriptions,
                          const Protobuf::int32 expected_error_code = Grpc::Status::GrpcStatus::Ok,
                          const std::string& expected_error_message = "");

  template <class T>
  void sendDiscoveryResponse(const std::vector<T>& added_or_updated,
                             const std::vector<std::string>& removed, const std::string& version) {
    envoy::api::v2::IncrementalDiscoveryResponse response;
    response.set_system_version_info("system_version_info_this_is_a_test");
    for (const auto& message : added_or_updated) {
      auto* resource = response.add_resources();
      resource->set_version(version);
      resource->mutable_resource()->PackFrom(message);
    }
    *response.mutable_removed_resources() = {removed.begin(), removed.end()};
    response.set_nonce("noncense");
    xds_stream_->sendGrpcMessage(response);
  }

protected:
  FakeUpstream* xds_upstream_{};
  FakeHttpConnectionPtr xds_connection_;
  FakeStreamPtr xds_stream_;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};

} // namespace Envoy
