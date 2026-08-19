#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/bootstrap/internal_listener/v3/internal_listener.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/server/filter_config.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Creates an empty TunnelResponseHeaders object on the downstream connection, marked
// SharedWithUpstreamConnection so that it is forward-shared to the internal connection.
class SeedPlaceholderFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    decoder_callbacks_->streamInfo().filterState()->setData(
        TcpProxy::TunnelResponseHeaders::key(),
        std::make_shared<TcpProxy::TunnelResponseHeaders>(),
        StreamInfo::FilterState::LifeSpan::Connection,
        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
    return Http::FilterHeadersStatus::Continue;
  }
};

class SeedPlaceholderFilterFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamFilter(std::make_shared<SeedPlaceholderFilter>());
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
  std::string name() const override { return "envoy.test.seed_placeholder"; }
};

REGISTER_FACTORY(SeedPlaceholderFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

// Topology: outer HCM -> internal_listener cluster carrying the internal_upstream transport
// socket -> internal listener running tcp_proxy with propagate_response_headers -> fake upstream
// that rejects the CONNECT.
class FwdMutablePlaceholderIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  FwdMutablePlaceholderIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::httpProxyConfig()) {
    setUpstreamCount(1);
  }

  // Object seeded by a downstream filter and read via %FILTER_STATE% when false; provisioned by
  // internal_upstream and read via %UPSTREAM_FILTER_STATE% when true.
  bool seed_via_hcm_filter_{true};
  bool read_upstream_filter_state_{false};
  // The wrapped socket's handshake travels through the CONNECT tunnel, so the pool cannot report
  // readiness until the CONNECT succeeds. Without a handshake, readiness is immediate.
  bool wrapped_socket_uses_tls_{false};

  void initialize() override {
    access_log_name_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename("fwd_fs"));

    if (seed_via_hcm_filter_) {
      config_helper_.prependFilter(R"EOF(
      name: envoy.test.seed_placeholder
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Struct
    )EOF");
    }

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::extensions::bootstrap::internal_listener::v3::InternalListener config;
      auto* bootstrap_extension = bootstrap.add_bootstrap_extensions();
      std::ignore = bootstrap_extension->mutable_typed_config()->PackFrom(config);
      bootstrap_extension->set_name("envoy.bootstrap.internal_listener");

      auto* static_resources = bootstrap.mutable_static_resources();

      auto* cluster = static_resources->mutable_clusters()->Add();
      cluster->set_name("internal_listener");
      cluster->clear_load_assignment();
      cluster->mutable_load_assignment()->set_cluster_name("internal_listener");
      auto* endpoint = cluster->mutable_load_assignment()
                           ->add_endpoints()
                           ->add_lb_endpoints()
                           ->mutable_endpoint();
      auto* addr = endpoint->mutable_address()->mutable_envoy_internal_address();
      addr->set_server_listener_name("internal_listener");
      addr->set_endpoint_id("lorem_ipsum");
      // Naming the factory makes internal_upstream provision the object; left unset, the seed
      // filter supplies it.
      const std::string provisioning_block =
          read_upstream_filter_state_
              ? "\n        provisioned_placeholder_factories:\n        - "
                "envoy.tcp_proxy.propagate_response_headers"
              : "";
      const std::string wrapped_transport_socket =
          wrapped_socket_uses_tls_
              ? fmt::format(R"EOF(
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
            common_tls_context:
              validation_context:
                trusted_ca:
                  filename: {})EOF",
                            TestEnvironment::runfilesPath(
                                "test/config/integration/certs/upstreamcacert.pem"))
              : std::string(R"EOF(
          name: envoy.transport_sockets.raw_buffer
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer)EOF");

      TestUtility::loadFromYaml(fmt::format(R"EOF(
      name: envoy.transport_sockets.internal_upstream
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.internal_upstream.v3.InternalUpstreamTransport
        transport_socket:{}{}
      )EOF",
                                            wrapped_transport_socket, provisioning_block),
                                *cluster->mutable_transport_socket());


      TestUtility::loadFromYaml(R"EOF(
      name: internal_listener
      internal_listener: {}
      filter_chains:
      - filters:
        - name: tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            cluster: cluster_0
            stat_prefix: internal_tunnel
            tunneling_config:
              hostname: host.com:443
              propagate_response_headers: true
      )EOF",
                                *static_resources->mutable_listeners()->Add());
    });

    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_route()
              ->set_cluster("internal_listener");
          const std::string fs_fmt =
              read_upstream_filter_state_
                  ? "%UPSTREAM_FILTER_STATE(envoy.tcp_proxy.propagate_response_headers:TYPED)%"
                  : "%FILTER_STATE(envoy.tcp_proxy.propagate_response_headers:TYPED)%";
          TestUtility::loadFromYaml(fmt::format(R"EOF(
          name: envoy.file_access_log
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
            path: {}
            log_format:
              text_format_source:
                inline_string: "{}\n"
          )EOF",
                                                access_log_name_, fs_fmt),
                                    *hcm.add_access_log());
        });

    HttpIntegrationTest::initialize();
  }
};

// A non-2xx CONNECT status is readable on the downstream connection's filter state.
TEST_F(FwdMutablePlaceholderIntegrationTest, ForwardMutablePlaceholderCarriesNon2xxStatus) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().getMethodValue(), "CONNECT");

  const std::string header_value = "secret-value";
  Http::TestResponseHeaderMapImpl response_headers{{":status", "403"}};
  response_headers.addCopy("test-header-name", header_value);
  upstream_request_->encodeHeaders(response_headers, false);

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());
  cleanupUpstreamAndDownstream();

  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr(header_value));
}

// internal_upstream provisions the object itself, with no seed filter, and it is read via
// %UPSTREAM_FILTER_STATE%.
TEST_F(FwdMutablePlaceholderIntegrationTest, BoundaryProvisionedPlaceholderViaUpstreamFilterState) {
  seed_via_hcm_filter_ = false;
  read_upstream_filter_state_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().getMethodValue(), "CONNECT");

  const std::string header_value = "secret-value";
  Http::TestResponseHeaderMapImpl response_headers{{":status", "403"}};
  response_headers.addCopy("test-header-name", header_value);
  upstream_request_->encodeHeaders(response_headers, false);

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());
  cleanupUpstreamAndDownstream();

  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr(header_value));
}

// The wrapped socket is TLS, so a rejected CONNECT makes the pool report failure rather than
// readiness. Covers recording upstream filter state on the failure path.
TEST_F(FwdMutablePlaceholderIntegrationTest, ProvisionedPlaceholderReadableOnPoolFailure) {
  seed_via_hcm_filter_ = false;
  read_upstream_filter_state_ = true;
  wrapped_socket_uses_tls_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().getMethodValue(), "CONNECT");

  const std::string header_value = "secret-value";
  Http::TestResponseHeaderMapImpl response_headers{{":status", "403"}};
  response_headers.addCopy("test-header-name", header_value);
  upstream_request_->encodeHeaders(response_headers, false);

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());
  cleanupUpstreamAndDownstream();

  // The connection must have failed to connect. If it reached readiness instead, this case would
  // not exercise the failure path at all.
  EXPECT_GT(test_server_->counter("cluster.internal_listener.upstream_cx_connect_fail")->value(), 0);

  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr(header_value));
}

} // namespace
} // namespace Envoy
