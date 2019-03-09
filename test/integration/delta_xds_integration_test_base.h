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

class DeltaXdsIntegrationTestBase : public HttpIntegrationTest {
public:
  DeltaXdsIntegrationTestBase(Http::CodecClient::Type downstream_protocol,
                              Network::Address::IpVersion version)
      : HttpIntegrationTest(downstream_protocol, version, realTime()) {}
  DeltaXdsIntegrationTestBase(Http::CodecClient::Type downstream_protocol,
                              Network::Address::IpVersion version, const std::string& config)
      : HttpIntegrationTest(downstream_protocol, version, realTime(), config) {}

  void createXdsConnection(FakeUpstream& upstream);

  void cleanUpXdsConnection();

protected:
  FakeUpstream* xds_upstream_{};
  FakeHttpConnectionPtr xds_connection_;
  FakeStreamPtr xds_stream_;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};

} // namespace Envoy
