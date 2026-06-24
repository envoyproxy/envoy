#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

using testing::Eq;
using testing::Ge;

class ListenerFcdsIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest,
                                    public HttpIntegrationTest {
public:
  ListenerFcdsIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {
    skip_tag_extraction_rule_check_ = true;
  }

  ~ListenerFcdsIntegrationTest() override {
    if (fcds_connection_ != nullptr) {
      AssertionResult result = fcds_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fcds_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      fcds_connection_.reset();
    }
    if (lds_connection_ != nullptr) {
      AssertionResult result = lds_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = lds_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      lds_connection_.reset();
    }
  }

  void addFcdsCluster(const std::string& cluster_name) {
    config_helper_.addConfigModifier(
        [cluster_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* fcds_cluster = bootstrap.mutable_static_resources()->add_clusters();
          fcds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          fcds_cluster->set_name(cluster_name);
          ConfigHelper::setHttp2(*fcds_cluster);
        });
  }

  void initialize() override {
    defer_listener_finalization_ = true;
    setUpstreamCount(1);

    addFcdsCluster("fcds_cluster");
    use_lds_ = false;

    // Add LDS cluster.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      lds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      lds_cluster->set_name("lds_cluster");
      ConfigHelper::setHttp2(*lds_cluster);
    });

    // Setup dynamic LDS config.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      listener_config_.Swap(bootstrap.mutable_static_resources()->mutable_listeners(0));
      listener_config_.set_name(listener_name_);

      const bool is_delta = (this->sotwOrDelta() == Grpc::SotwOrDelta::Delta ||
                             this->sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta);
      // Setup dynamic FCDS config on the listener template.
      auto* fcds_config = listener_config_.mutable_fcds_config();
      fcds_config->mutable_config_source()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      auto* api_config_source = fcds_config->mutable_config_source()->mutable_api_config_source();
      api_config_source->set_api_type(is_delta
                                          ? envoy::config::core::v3::ApiConfigSource::DELTA_GRPC
                                          : envoy::config::core::v3::ApiConfigSource::GRPC);
      api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* grpc_service = api_config_source->add_grpc_services();
      setGrpcService(*grpc_service, "fcds_cluster", getFcdsFakeUpstream().localAddress());

      // Add a dynamic filter chain reference.
      listener_config_.mutable_filter_chains()->Clear();
      auto* dynamic_fc = listener_config_.add_filter_chains();
      dynamic_fc->set_name("dynamic_filter_chain_1");

      const std::string matcher_yaml = R"EOF(
        matcher_tree:
          input:
            name: port
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationPortInput
          exact_match_map:
            map:
              "10000":
                action:
                  name: filter-chain-name
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: dynamic_filter_chain_1
        on_no_match:
          action:
            name: filter-chain-name
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: dynamic_filter_chain_1
      )EOF";
      TestUtility::loadFromYaml(matcher_yaml, *listener_config_.mutable_filter_chain_matcher());

      if (two_listeners_) {
        listener_config2_ = listener_config_;
        listener_config2_.set_name("testing-listener-1");
      }

      bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
      auto* lds_config_source = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
      lds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* lds_api_config_source = lds_config_source->mutable_api_config_source();
      lds_api_config_source->set_api_type(is_delta
                                              ? envoy::config::core::v3::ApiConfigSource::DELTA_GRPC
                                              : envoy::config::core::v3::ApiConfigSource::GRPC);
      lds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
      auto* lds_grpc_service = lds_api_config_source->add_grpc_services();
      setGrpcService(*lds_grpc_service, "lds_cluster", getLdsFakeUpstream().localAddress());
    });

    HttpIntegrationTest::initialize();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create the LDS upstream (fake_upstreams_[1]).
    addFakeUpstream(Http::CodecType::HTTP2);
    // Create the FCDS upstream (fake_upstreams_[2]).
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  FakeUpstream& getLdsFakeUpstream() const { return *fake_upstreams_[2]; }
  FakeUpstream& getFcdsFakeUpstream() const { return *fake_upstreams_[1]; }

  void waitXdsStream(const std::vector<envoy::config::listener::v3::Listener>& listeners) {
    // Wait for LDS connection.
    auto& lds_upstream = getLdsFakeUpstream();
    AssertionResult result = lds_upstream.waitForHttpConnection(*dispatcher_, lds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = lds_connection_->waitForNewStream(*dispatcher_, lds_stream_);
    RELEASE_ASSERT(result, result.message());
    lds_stream_->startGrpcStream();

    // Send LDS response pointing to dynamic filter chains.
    sendLdsResponse(listeners, "1");

    // Wait for FCDS connection.
    auto& fcds_upstream = getFcdsFakeUpstream();
    result = fcds_upstream.waitForHttpConnection(*dispatcher_, fcds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fcds_connection_->waitForNewStream(*dispatcher_, fcds_stream_);
    RELEASE_ASSERT(result, result.message());
    fcds_stream_->startGrpcStream();
  }

  void waitXdsStream() { waitXdsStream({listener_config_}); }

  void sendLdsResponse(const std::vector<envoy::config::listener::v3::Listener>& listeners,
                       const std::string& version) {
    if (this->sotwOrDelta() == Grpc::SotwOrDelta::Delta ||
        this->sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta) {
      sendDeltaDiscoveryResponse(Config::TestTypeUrl::get().Listener, listeners, {}, version,
                                 lds_stream_.get());
    } else {
      envoy::service::discovery::v3::DiscoveryResponse response;
      response.set_version_info(version);
      response.set_type_url(Config::TestTypeUrl::get().Listener);
      for (const auto& listener : listeners) {
        response.add_resources()->PackFrom(listener);
      }
      lds_stream_->sendGrpcMessage(response);
    }
  }

  void sendLdsResponse(const std::string& version) { sendLdsResponse({listener_config_}, version); }

  envoy::config::listener::v3::FilterChain buildFilterChain(const std::string& name,
                                                            int direct_response_status) {
    envoy::config::listener::v3::FilterChain filter_chain;
    filter_chain.set_name(name);
    auto* filter = filter_chain.add_filters();
    filter->set_name("envoy.filters.network.http_connection_manager");

    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager hcm;
    hcm.set_stat_prefix("fcds_test");
    auto* virtual_host = hcm.mutable_route_config()->add_virtual_hosts();
    virtual_host->set_name("fcds_vhost");
    virtual_host->add_domains("*");
    auto* route = virtual_host->add_routes();
    route->mutable_match()->set_prefix("/");
    route->mutable_direct_response()->set_status(direct_response_status);
    route->mutable_direct_response()->mutable_body()->set_inline_string("fcds body");

    auto* router = hcm.add_http_filters();
    router->set_name("envoy.filters.http.router");
    router->mutable_typed_config()->PackFrom(
        envoy::extensions::filters::http::router::v3::Router());
    filter->mutable_typed_config()->PackFrom(hcm);
    return filter_chain;
  }

  void
  sendFcdsResponse(const std::vector<envoy::config::listener::v3::FilterChain>& added_or_updated,
                   const std::vector<std::string>& removed, const std::string& version) {
    if (this->sotwOrDelta() == Grpc::SotwOrDelta::Delta ||
        this->sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta) {
      sendDeltaDiscoveryResponse(Config::TestTypeUrl::get().FilterChain, added_or_updated, removed,
                                 version, fcds_stream_.get());
    } else {
      envoy::service::discovery::v3::DiscoveryResponse response;
      response.set_version_info(version);
      response.set_type_url(Config::TestTypeUrl::get().FilterChain);
      for (const auto& filter_chain : added_or_updated) {
        response.add_resources()->PackFrom(filter_chain);
      }
      fcds_stream_->sendGrpcMessage(response);
    }
  }

  void sendFcdsResponse(const std::vector<envoy::config::listener::v3::FilterChain>& filter_chains,
                        const std::string& version) {
    sendFcdsResponse(filter_chains, {}, version);
  }

  envoy::config::listener::v3::Listener listener_config_;
  envoy::config::listener::v3::Listener listener_config2_;
  bool two_listeners_{false};
  std::string listener_name_{"testing-listener-0"};
  FakeHttpConnectionPtr lds_connection_;
  FakeHttpConnectionPtr fcds_connection_;
  FakeStreamPtr lds_stream_;
  FakeStreamPtr fcds_stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, ListenerFcdsIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Tests that the listener successfully warms using FCDS dynamic filter chains
// and performs in-place updates when the filter chain is modified.
TEST_P(ListenerFcdsIntegrationTest, BasicFcdsInPlaceUpdate) {
  on_server_init_function_ = [&]() {
    waitXdsStream();
    // Resolve warming by sending FCDS response with 200 direct response config.
    sendFcdsResponse({buildFilterChain("dynamic_filter_chain_1", 200)}, "1");
  };
  initialize();
  registerTestServerPorts({listener_name_});

  // Wait for the listener to be active and listening on workers.
  test_server_->waitForCounter("listener_manager.listener_create_success", Ge(1));

  // Verify dynamic filter chain serves HTTP 200 correctly.
  IntegrationCodecClientPtr codec_client = makeHttpConnection(lookupPort(listener_name_));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  IntegrationStreamDecoderPtr response = codec_client->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("fcds body", response->body());
  codec_client->close();

  // Perform in-place FCDS update modifying the direct response status code to 404!
  sendFcdsResponse({buildFilterChain("dynamic_filter_chain_1", 404)}, "2");

  // Wait for Envoy's local stats to increment indicating FCDS update has completed
  test_server_->waitForCounter("filter_chain_manager.dynamic_filter_chain_1.update_success",
                               Eq(2));
  // Wait for the new config to take effect. Since there's no listener reconstruction,
  // we just make a new connection.
  IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort(listener_name_));
  IntegrationStreamDecoderPtr response2 = codec_client2->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("404", response2->headers().getStatusValue());
  codec_client2->close();

  // Assert that the listener was NOT added or created again (meaning no reconstruction).
  // Total added must remain 1.
  EXPECT_EQ(1, test_server_->counter("listener_manager.listener_added")->value());
}

TEST_P(ListenerFcdsIntegrationTest, TwoListenersSharedFilterChain) {
  two_listeners_ = true;
  on_server_init_function_ = [&]() {
    waitXdsStream({listener_config_, listener_config2_});
    // Resolve warming by sending FCDS response with 200 direct response config.
    sendFcdsResponse({buildFilterChain("dynamic_filter_chain_1", 200)}, "1");
  };
  initialize();
  registerTestServerPorts({listener_name_, "testing-listener-1"});

  // Wait for BOTH listeners to be active and listening.
  test_server_->waitForCounter("listener_manager.listener_create_success", Ge(2));

  // Verify both listeners serve HTTP 200 correctly.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};

  IntegrationCodecClientPtr codec_client1 = makeHttpConnection(lookupPort(listener_name_));
  IntegrationStreamDecoderPtr response1 = codec_client1->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  codec_client1->close();

  IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort("testing-listener-1"));
  IntegrationStreamDecoderPtr response2 = codec_client2->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response2->headers().getStatusValue());
  codec_client2->close();

  // Perform in-place FCDS update modifying the direct response status code to 404!
  sendFcdsResponse({buildFilterChain("dynamic_filter_chain_1", 404)}, "2");

  // Wait for Envoy's local stats to increment indicating FCDS update has completed
  test_server_->waitForCounter("filter_chain_manager.dynamic_filter_chain_1.update_success",
                               Eq(2));

  // Verify both listeners serve HTTP 404 correctly.
  IntegrationCodecClientPtr codec_client1_updated = makeHttpConnection(lookupPort(listener_name_));
  IntegrationStreamDecoderPtr response1_updated =
      codec_client1_updated->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response1_updated->waitForEndStream());
  EXPECT_EQ("404", response1_updated->headers().getStatusValue());
  codec_client1_updated->close();

  IntegrationCodecClientPtr codec_client2_updated =
      makeHttpConnection(lookupPort("testing-listener-1"));
  IntegrationStreamDecoderPtr response2_updated =
      codec_client2_updated->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response2_updated->waitForEndStream());
  EXPECT_EQ("404", response2_updated->headers().getStatusValue());
  codec_client2_updated->close();

  // Total listeners added must be 2.
  EXPECT_EQ(2, test_server_->counter("listener_manager.listener_added")->value());
}

TEST_P(ListenerFcdsIntegrationTest, FcdsFilterChainRemovalAndDraining) {
  // SotW gRPC subscriptions do not support dynamic resource deletion notifications
  // from empty discovery responses. Skip the test case for SotW runs.
  if (this->sotwOrDelta() == Grpc::SotwOrDelta::Sotw ||
      this->sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw) {
    GTEST_SKIP();
  }

  // Use HTTP2 to test GOAWAY behavior.
  downstream_protocol_ = Http::CodecType::HTTP2;

  // Set a very short drain time so the test doesn't take long.
  setDrainTime(std::chrono::seconds(1));

  on_server_init_function_ = [&]() {
    waitXdsStream();
    // Resolve warming by sending FCDS response with 200 direct response config.
    sendFcdsResponse({buildFilterChain("dynamic_filter_chain_1", 200)}, "1");
  };
  initialize();
  registerTestServerPorts({listener_name_});

  test_server_->waitForCounter("listener_manager.listener_create_success", Ge(1));

  // Make an active connection.
  IntegrationCodecClientPtr codec_client = makeHttpConnection(lookupPort(listener_name_));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  IntegrationStreamDecoderPtr response = codec_client->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Now perform in-place FCDS update removing the filter chain.
  // We send an empty list of filter chains, which means dynamic_filter_chain_1 is removed.
  sendFcdsResponse({}, {"dynamic_filter_chain_1"}, "2");

  // Wait for Envoy's local stats to increment indicating FCDS update has completed.
  test_server_->waitForCounter("filter_chain_manager.dynamic_filter_chain_1.update_success",
                               Eq(2));

  // Check that the filter chain is now in draining state!
  test_server_->waitForGauge("listener_manager.total_filter_chains_draining", Eq(1));

  // Make a second request on the same connection. This should trigger the GOAWAY frame.
  IntegrationStreamDecoderPtr response2 = codec_client->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  // Verify that the client saw the GOAWAY frame!
  EXPECT_TRUE(codec_client->sawGoAway());

  // Wait for the client connection to be disconnected (which happens after GOAWAY is processed and drain timer fires).
  ASSERT_TRUE(codec_client->waitForDisconnect());

  // Wait for the draining filter chain count to drop to 0.
  test_server_->waitForGauge("listener_manager.total_filter_chains_draining", Eq(0));
}

} // namespace
} // namespace Envoy
