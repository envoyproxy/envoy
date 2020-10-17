#pragma once

#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

namespace Envoy {
class Http2IntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                             public HttpIntegrationTest {
public:
  Http2IntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void SetUp() override { setDownstreamProtocol(Http::CodecClient::Type::HTTP2); }

  void simultaneousRequest(int32_t request1_bytes, int32_t request2_bytes);

protected:
  // Utility function to add filters.
  void addFilters(std::vector<std::string> filters) {
    for (const auto& filter : filters) {
      config_helper_.addFilter(filter);
    }
  }
};

class Http2RingHashIntegrationTest : public Http2IntegrationTest {
public:
  Http2RingHashIntegrationTest();

  ~Http2RingHashIntegrationTest() override;

  void createUpstreams() override;

  void sendMultipleRequests(int request_bytes, Http::TestRequestHeaderMapImpl headers,
                            std::function<void(IntegrationStreamDecoder&)> cb);

  std::vector<FakeHttpConnectionPtr> fake_upstream_connections_;
  int num_upstreams_ = 5;
};

class Http2MetadataIntegrationTest : public Http2IntegrationTest {
public:
  void SetUp() override {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          cluster->mutable_http2_protocol_options()->set_allow_metadata(true);
        });
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  }

  void testRequestMetadataWithStopAllFilter();

  void verifyHeadersOnlyTest();

  void runHeaderOnlyTest(bool send_request_body, size_t body_size);
};

} // namespace Envoy
