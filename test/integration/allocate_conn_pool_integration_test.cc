#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"

#include "source/common/http/message_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {

class MyCallbacks : public Http::AsyncClient::Callbacks {
public:
  MyCallbacks(absl::Notification& done) : done_(done) {}
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override {
    done_.Notify();
  }
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override {
    done_.Notify();
  }
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

private:
  absl::Notification& done_;
};

class AllocateConnPoolIntegrationTest : public HttpIntegrationTest,
                                        public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AllocateConnPoolIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void SetUp() override { setUpstreamProtocol(Http::CodecType::HTTP3); }

  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          envoy::extensions::upstreams::http::v3::HttpProtocolOptions protocol_options;
          auto* auto_config = protocol_options.mutable_auto_config();
          auto_config->mutable_http3_protocol_options();
          auto_config->mutable_alternate_protocols_cache_options()->set_name("base_cache");
          ConfigHelper::setProtocolOptions(*cluster, protocol_options);

          // Configure QUIC for the upstream cluster to satisfy validation.
          auto* transport_socket = cluster->mutable_transport_socket();
          transport_socket->set_name("envoy.transport_sockets.quic");
          envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport quic_context;
          quic_context.mutable_upstream_tls_context();
          std::ignore = transport_socket->mutable_typed_config()->PackFrom(quic_context);
        });
    config_helper_.addRuntimeOverride("upstream.use_http3", "true");
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AllocateConnPoolIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AllocateConnPoolIntegrationTest, NullTransportSocketOptionsCrash) {
  initialize();

  absl::Notification done;
  auto callbacks = std::make_shared<MyCallbacks>(done);

  test_server_->server().dispatcher().post([&, callbacks]() {
    auto cluster = test_server_->server().clusterManager().getThreadLocalCluster("cluster_0");
    if (cluster != nullptr) {
      auto& async_client = cluster->httpAsyncClient();

      Http::TestRequestHeaderMapImpl headers{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "127.0.0.1"}};

      auto message = std::make_unique<Http::RequestMessageImpl>(
          std::make_unique<Http::TestRequestHeaderMapImpl>(headers));

      // Verify that this doesn't crash the server.
      async_client.send(std::move(message), *callbacks, Http::AsyncClient::RequestOptions());
    }
  });
  done.WaitForNotification();
}
} // namespace Envoy
