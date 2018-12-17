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

class XdsIntegrationTestBase : public HttpIntegrationTest {
public:
  XdsIntegrationTestBase(Http::CodecClient::Type downstream_protocol,
                         Network::Address::IpVersion version)
      : HttpIntegrationTest(downstream_protocol, version, realTime()) {}
  XdsIntegrationTestBase(Http::CodecClient::Type downstream_protocol,
                         Network::Address::IpVersion version, const std::string& config)
      : HttpIntegrationTest(downstream_protocol, version, realTime(), config) {}

  void createXdsConnection(FakeUpstream& upstream);

  void cleanUpXdsConnection();

  AssertionResult
  compareDiscoveryRequest(const std::string& expected_type_url, const std::string& expected_version,
                          const std::vector<std::string>& expected_resource_names,
                          const Protobuf::int32 expected_error_code = Grpc::Status::GrpcStatus::Ok,
                          const std::string& expected_error_message = "");

  template <class T>
  void sendDiscoveryResponse(const std::string& type_url, const std::vector<T>& messages,
                             const std::string& version) {
    envoy::api::v2::DiscoveryResponse discovery_response;
    discovery_response.set_version_info(version);
    discovery_response.set_type_url(type_url);
    for (const auto& message : messages) {
      discovery_response.add_resources()->PackFrom(message);
    }
    xds_stream_->sendGrpcMessage(discovery_response);
  }

protected:
  FakeUpstream* xds_upstream_{};
  FakeHttpConnectionPtr xds_connection_;
  FakeStreamPtr xds_stream_;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};

} // namespace Envoy
