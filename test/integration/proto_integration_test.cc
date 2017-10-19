#include <string>

#include "common/config/well_known_names.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"

#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// TODO(alyssawilk) move these test into integration_test.cc
class ProtoIntegrationTest : public HttpIntegrationTest,
                             public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ProtoIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}
};

TEST_P(ProtoIntegrationTest, TestBind) {
  std::string address_string;
  if (GetParam() == Network::Address::IpVersion::v4) {
    address_string = TestUtility::getIpv4Loopback();
  } else {
    address_string = "::1";
  }
  config_helper_.setSourceAddress(address_string);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Request 1.

  codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                             {":path", "/test/long/url"},
                                                             {":scheme", "http"},
                                                             {":authority", "host"}},
                                     1024, *response_);

  fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
  std::string address =
      fake_upstream_connection_->connection().remoteAddress().ip()->addressAsString();
  EXPECT_EQ(address, address_string);
  upstream_request_ = fake_upstream_connection_->waitForNewStream(*dispatcher_);
  upstream_request_->waitForEndStream(*dispatcher_);
  // Cleanup both downstream and upstream
  codec_client_->close();
  fake_upstream_connection_->close();
  fake_upstream_connection_->waitForDisconnect();
}

TEST_P(ProtoIntegrationTest, TestFailedBind) {
  config_helper_.setSourceAddress("8.8.8.8");

  initialize();
  // Envoy will create and close some number of connections when trying to bind.
  // Make sure they don't cause assertion failures when we ignore them.
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // With no ability to successfully bind on an upstream connection Envoy should
  // send a 500.
  codec_client_->makeHeaderOnlyRequest(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"x-forwarded-for", "10.0.0.1"},
                              {"x-envoy-upstream-rq-timeout-ms", "1000"}},
      *response_);
  response_->waitForEndStream();
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("503", response_->headers().Status()->value().c_str());
  EXPECT_LT(0, test_server_->counter("cluster.cluster_0.bind_errors")->value());
}

INSTANTIATE_TEST_CASE_P(IpVersions, ProtoIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));
} // namespace
} // namespace Envoy
