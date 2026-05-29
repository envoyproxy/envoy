#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/grpc/status.h"

#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"
#include "source/common/version/version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/ads_integration.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {

// Tests for cases where both (old) ADS and xDS-TP based config sources are
// defined in the bootstrap.
class AdsXdsTpConfigsIntegrationTest : public AdsDeltaSotwIntegrationSubStateParamTest,
                                       public HttpIntegrationTest {
public:
  AdsXdsTpConfigsIntegrationTest()
      : HttpIntegrationTest(
            Http::CodecType::HTTP2, ipVersion(),
            ConfigHelper::adsBootstrap((sotwOrDelta() == Grpc::SotwOrDelta::Sotw) ||
                                               (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw)
                                           ? "GRPC"
                                           : "DELTA_GRPC")) {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw ||
                                       sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta)
                                          ? "true"
                                          : "false");
    use_lds_ = false;
    // xds_upstream_ will be used for the ADS upstream.
    create_xds_upstream_ = true;
    // Not testing TLS in this case.
    tls_xds_upstream_ = false;
    sotw_or_delta_ = sotwOrDelta();
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  FakeUpstream* createAdsUpstream() {
    ASSERT(!tls_xds_upstream_);
    addFakeUpstream(Http::CodecType::HTTP2);
    return fake_upstreams_.back().get();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // An upstream for authority1 (H/2), an upstream for the default_authority (H/2), and an
    // upstream for a backend (H/1).
    authority1_upstream_ = createAdsUpstream();
    default_authority_upstream_ = createAdsUpstream();
  }

  void TearDown() override {
    cleanupXdsConnection(xds_connection_);
    cleanupXdsConnection(authority1_xds_connection_);
    cleanupXdsConnection(default_authority_xds_connection_);
  }

  void cleanupXdsConnection(FakeHttpConnectionPtr& connection) {
    if (connection != nullptr) {
      AssertionResult result = connection->close();
      RELEASE_ASSERT(result, result.message());
      result = connection->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection.reset();
    }
  }

  bool isSotw() const {
    return sotwOrDelta() == Grpc::SotwOrDelta::Sotw ||
           sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw;
  }

  // Adds config_source for authority1.com and a default_config_source for
  // default_authority.com.
  void initialize() override {
    config_helper_.addRuntimeOverride(
        "envoy.reloadable_features.xdstp_based_config_singleton_subscriptions", "true");
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add the first config_source.
      {
        auto* config_source1 = bootstrap.mutable_config_sources()->Add();
        config_source1->mutable_authorities()->Add()->set_name("authority1.com");
        auto* api_config_source = config_source1->mutable_api_config_source();
        api_config_source->set_api_type(
            isSotw() ? envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC
                     : envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC);
        api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
        api_config_source->set_set_node_on_first_message_only(true);
        auto* grpc_service = api_config_source->add_grpc_services();
        setGrpcService(*grpc_service, "authority1_cluster", authority1_upstream_->localAddress());
        auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
        xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        xds_cluster->set_name("authority1_cluster");
      }
      // Add the default config source.
      {
        auto* default_config_source = bootstrap.mutable_default_config_source();
        default_config_source->mutable_authorities()->Add()->set_name("default_authority.com");
        auto* api_config_source = default_config_source->mutable_api_config_source();
        api_config_source->set_api_type(
            isSotw() ? envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC
                     : envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC);
        api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
        api_config_source->set_set_node_on_first_message_only(true);
        auto* grpc_service = api_config_source->add_grpc_services();
        setGrpcService(*grpc_service, "default_authority_cluster",
                       default_authority_upstream_->localAddress());
        auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
        xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        xds_cluster->set_name("default_authority_cluster");
      }
      // Add the (old) ADS server.
      {
        auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
        if (ads_config_type_override_.has_value()) {
          ads_config->set_api_type(ads_config_type_override_.value());
        } else {
          ads_config->set_api_type(isSotw() ? envoy::config::core::v3::ApiConfigSource::GRPC
                                            : envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
        }
        ads_config->set_transport_api_version(envoy::config::core::v3::V3);
        ads_config->set_set_node_on_first_message_only(true);
        auto* grpc_service = ads_config->add_grpc_services();
        setGrpcService(*grpc_service, "ads_cluster", xds_upstream_->localAddress());
        auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
        ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        ads_cluster->set_name("ads_cluster");
      }
    });
    HttpIntegrationTest::initialize();
    connectAds();
    connectAuthority1();
    connectDefaultAuthority();
  }

  void connectAds() {
    if (xds_stream_ == nullptr) {
      AssertionResult result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
      RELEASE_ASSERT(result, result.message());
      result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      xds_stream_->startGrpcStream();
    }
  }

  void connectAuthority1() {
    AssertionResult result =
        authority1_upstream_->waitForHttpConnection(*dispatcher_, authority1_xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = authority1_xds_connection_->waitForNewStream(*dispatcher_, authority1_xds_stream_);
    RELEASE_ASSERT(result, result.message());
    authority1_xds_stream_->startGrpcStream();
  }

  void connectDefaultAuthority() {
    AssertionResult result = default_authority_upstream_->waitForHttpConnection(
        *dispatcher_, default_authority_xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = default_authority_xds_connection_->waitForNewStream(*dispatcher_,
                                                                 default_authority_xds_stream_);
    RELEASE_ASSERT(result, result.message());
    default_authority_xds_stream_->startGrpcStream();
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name) {
    // The first fake upstream is the emulated backend server.
    return ConfigHelper::buildClusterLoadAssignment(
        name, Network::Test::getLoopbackAddressString(ipVersion()),
        fake_upstreams_[0].get()->localAddress()->ip()->port());
  }

  void setupClustersFromOldAds() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->mutable_ads();
    });
  }

  void setupListenersFromOldAds() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_dynamic_resources()->mutable_lds_config()->mutable_ads();
    });
  }

  envoy::config::listener::v3::Listener buildListener(const std::string& name,
                                                      const std::string& route_config) {
    return ConfigHelper::buildListener(
        name, route_config, Network::Test::getLoopbackAddressString(ipVersion()), "ads_test");
  }

  void makeSingleRequest() {
    registerTestServerPorts({"http"});
    testRouterHeaderOnlyRequestAndResponse();
    cleanupUpstreamAndDownstream();
  }

  // Data members that emulate the authority1 server.
  FakeUpstream* authority1_upstream_;
  FakeHttpConnectionPtr authority1_xds_connection_;
  FakeStreamPtr authority1_xds_stream_;

  // Data members that emulate the default_authority server.
  FakeUpstream* default_authority_upstream_;
  FakeHttpConnectionPtr default_authority_xds_connection_;
  FakeStreamPtr default_authority_xds_stream_;

  // An optional setting to overwrite the (old) ADS config type. By default it
  // is not set, and the integration test param value will be used.
  absl::optional<envoy::config::core::v3::ApiConfigSource_ApiType> ads_config_type_override_;
};

} // namespace Envoy
