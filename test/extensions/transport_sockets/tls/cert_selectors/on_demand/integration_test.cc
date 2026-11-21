#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener_filter_buffer.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include "test/config/integration/certs/clientcert_hash.h"
#include "test/config/integration/certs/server2cert_hash.h"
#include "test/config/integration/certs/servercert_hash.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/tcp_proxy_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

using testing::Eq;
namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {
namespace {

// Thread-safe dynamic filter state controller used by multi-tenant integration tests to vary
// per-connection tenant IDs (and upstream secret names) across sequential connections on the same
// SecretManager.
struct DynamicFilterState {
  absl::Mutex mutex;
  std::string tenant_id ABSL_GUARDED_BY(mutex);
  std::string secret_name ABSL_GUARDED_BY(mutex);

  void setTenant(absl::string_view tenant) {
    absl::MutexLock lock(mutex);
    tenant_id = std::string(tenant);
  }
  void setUpstream(absl::string_view secret, absl::string_view tenant) {
    absl::MutexLock lock(mutex);
    secret_name = std::string(secret);
    tenant_id = std::string(tenant);
  }
};

class DynamicTenantListenerFilter : public Network::ListenerFilter {
public:
  explicit DynamicTenantListenerFilter(std::shared_ptr<DynamicFilterState> state)
      : state_(std::move(state)) {}

  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override {
    std::string tenant;
    {
      absl::MutexLock lock(state_->mutex);
      tenant = state_->tenant_id;
    }
    if (!tenant.empty()) {
      cb.streamInfo().filterState()->setData(TenantIdFilterStateKey,
                                             std::make_shared<Router::StringAccessorImpl>(tenant),
                                             StreamInfo::FilterState::LifeSpan::Connection);
    }
    return Network::FilterStatus::Continue;
  }

  size_t maxReadBytes() const override { return 0; }
  Network::FilterStatus onData(Network::ListenerFilterBuffer&) override {
    return Network::FilterStatus::Continue;
  }

private:
  std::shared_ptr<DynamicFilterState> state_;
};

class DynamicTenantListenerFilterFactory
    : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  explicit DynamicTenantListenerFilterFactory(std::shared_ptr<DynamicFilterState> state)
      : state_(std::move(state)) {}

  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message&,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext&) override {
    return [listener_filter_matcher,
            state = state_](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(listener_filter_matcher,
                                     std::make_unique<DynamicTenantListenerFilter>(state));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "test.filters.listener.dynamic_tenant"; }

private:
  std::shared_ptr<DynamicFilterState> state_;
};

class DynamicUpstreamFilterStateReadFilter : public Network::ReadFilter {
public:
  explicit DynamicUpstreamFilterStateReadFilter(std::shared_ptr<DynamicFilterState> state)
      : state_(std::move(state)) {}

  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override {
    std::string secret;
    std::string tenant;
    {
      absl::MutexLock lock(state_->mutex);
      secret = state_->secret_name;
      tenant = state_->tenant_id;
    }
    auto& filter_state = *read_callbacks_->connection().streamInfo().filterState();
    if (!secret.empty()) {
      filter_state.setData("envoy.tls.certificate_mappers.on_demand_secret",
                           std::make_shared<Router::StringAccessorImpl>(secret),
                           StreamInfo::FilterState::LifeSpan::Connection,
                           StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
    }
    if (!tenant.empty()) {
      filter_state.setData(TenantIdFilterStateKey,
                           std::make_shared<Router::StringAccessorImpl>(tenant),
                           StreamInfo::FilterState::LifeSpan::Connection,
                           StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
    }
    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  std::shared_ptr<DynamicFilterState> state_;
  Network::ReadFilterCallbacks* read_callbacks_{nullptr};
};

class DynamicUpstreamFilterStateNetworkFilterFactory
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  explicit DynamicUpstreamFilterStateNetworkFilterFactory(std::shared_ptr<DynamicFilterState> state)
      : state_(std::move(state)) {}

  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return [state = state_](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<DynamicUpstreamFilterStateReadFilter>(state));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "test.filters.network.dynamic_upstream_filter_state"; }

private:
  std::shared_ptr<DynamicFilterState> state_;
};

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
const envoy::service::secret::v3::SdsDummy _sds_dummy;

struct TestParams {
  Network::Address::IpVersion ip_version;
  bool upstream;
};

std::string testParamsToString(const testing::TestParamInfo<TestParams>& p) {
  return fmt::format("{}_{}", TestUtility::ipVersionToString(p.param.ip_version),
                     p.param.upstream ? "Upstream" : "Downstream");
}

std::vector<TestParams> testParams() {
  std::vector<TestParams> ret;
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    ret.push_back(TestParams{ip_version, true});
    ret.push_back(TestParams{ip_version, false});
  }
  return ret;
}

class OnDemandIntegrationTest : public BaseTcpProxySslIntegrationTest,
                                public testing::TestWithParam<TestParams> {
public:
  OnDemandIntegrationTest()
      : BaseTcpProxySslIntegrationTest(GetParam().ip_version),
        upstream_selector_(GetParam().upstream),
        dynamic_filter_state_(std::make_shared<DynamicFilterState>()),
        listener_tenant_factory_(dynamic_filter_state_),
        listener_tenant_reg_(listener_tenant_factory_),
        upstream_filter_state_factory_(dynamic_filter_state_),
        upstream_filter_state_reg_(upstream_filter_state_factory_) {}

  void TearDown() override {
    cleanUpXdsConnection();
    extra_contexts_.clear();
  }

  void setup(const std::string& config = "") {
    const std::string on_demand_config = config.empty() ? defaultConfig() : config;
    config_helper_.addConfigModifier([this, on_demand_config](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()->add_clusters()->MergeFrom(
          bootstrap.static_resources().clusters(0));
      auto* sds_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      sds_cluster->set_name("sds_cluster");
      sds_cluster->mutable_load_assignment()->set_cluster_name("sds_cluster");
      ConfigHelper::setHttp2(*sds_cluster);
      if (upstream_selector_) {
        bootstrap.mutable_static_resources()
            ->mutable_listeners(0)
            ->mutable_filter_chains(0)
            ->clear_transport_socket();
        auto* backend = bootstrap.mutable_static_resources()->mutable_clusters(1);
        auto* transport_socket = backend->mutable_transport_socket();
        transport_socket->set_name("envoy.transport_sockets.tls");
        envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
        configToUseSds(*tls_context.mutable_common_tls_context(), on_demand_config);
        std::ignore = transport_socket->mutable_typed_config()->PackFrom(tls_context);
        if (use_dynamic_upstream_filter_state_) {
          const std::string dynamic_filter = R"EOF(
          name: test.filters.network.dynamic_upstream_filter_state
          typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
          )EOF";
          envoy::config::listener::v3::Filter filter;
          TestUtility::loadFromYaml(dynamic_filter, filter);
          auto* filter_chain =
              bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
          filter_chain->add_filters()->MergeFrom(filter_chain->filters(0));
          filter_chain->mutable_filters(0)->Swap(&filter);
        } else if (!filter_state_value_.empty()) {
          const std::string set_filter_state = fmt::format(R"EOF(
          name: envoy.filters.network.set_filter_state
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
            on_new_connection:
            - object_key: envoy.tls.certificate_mappers.on_demand_secret
              factory_key: envoy.string
              format_string:
                text_format_source:
                  inline_string: "{}"
              shared_with_upstream: ONCE
          )EOF",
                                                           filter_state_value_);
          envoy::config::listener::v3::Filter filter;
          TestUtility::loadFromYaml(set_filter_state, filter);
          auto* filter_chain =
              bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
          filter_chain->add_filters()->MergeFrom(filter_chain->filters(0));
          filter_chain->mutable_filters(0)->Swap(&filter);
        }
      } else {
        auto* filter_chain =
            bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
        auto* transport_socket = filter_chain->mutable_transport_socket();
        transport_socket->set_name("envoy.transport_sockets.tls");
        envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
        configToUseSds(*tls_context.mutable_common_tls_context(), on_demand_config);
        tls_context.set_disable_stateless_session_resumption(true);
        tls_context.set_disable_stateful_session_resumption(true);
        tls_context.mutable_require_client_certificate()->set_value(mtls_);
        std::ignore = transport_socket->mutable_typed_config()->PackFrom(tls_context);
      }
    });
    BaseTcpProxySslIntegrationTest::initialize();
    test_server_->waitUntilListenersReady();
  }

  std::string defaultConfig() const {
    return R"EOF(
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
      )EOF";
  }

  void configToUseSds(
      envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context,
      const std::string& on_demand_config) {
    common_tls_context.add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

    if (validation_sds_) {
      auto* validation_context = common_tls_context.mutable_validation_context_sds_secret_config();
      validation_context->set_name(cacert());
      setConfigSource(validation_context->mutable_sds_config());
    } else {
      auto* validation_context = common_tls_context.mutable_validation_context();
      if (upstream_selector_) {
        validation_context->mutable_trusted_ca()->set_filename(
            TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
      } else {
        validation_context->mutable_trusted_ca()->set_filename(
            TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
        validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);
      }
    }

    // Parse on-demand TLS cert selector config.
    envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config
        on_demand;
    TestUtility::loadFromYaml(on_demand_config, on_demand);

    // Configure config source
    setConfigSource(on_demand.mutable_config_source());
    common_tls_context.mutable_custom_tls_certificate_selector()->set_name("on-demand-config");
    std::ignore = common_tls_context.mutable_custom_tls_certificate_selector()
                      ->mutable_typed_config()
                      ->PackFrom(on_demand);
  }

  void setConfigSource(envoy::config::core::v3::ConfigSource* config_source) {
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    auto* grpc_service = api_config_source->add_grpc_services();
    grpc_service->mutable_timeout()->set_seconds(300);
    grpc_service->mutable_envoy_grpc()->set_cluster_name("sds_cluster");
  }

  void createUpstreams() override {
    // SDS cluster is H2, while the data cluster is H1.
    addFakeUpstream(Http::CodecType::HTTP2);
    if (upstream_selector_) {
      addFakeUpstream(createUpstreamTlsContext(upstreamConfig()), Http::CodecType::HTTP1, false);
    } else {
      addFakeUpstream(Http::CodecType::HTTP1);
    }
    xds_upstream_ = fake_upstreams_.front().get();
  }

  FakeUpstream* dataStream() override { return fake_upstreams_.back().get(); }

  std::unique_ptr<TestClientConnection> createClientConnection() {
    if (upstream_selector_) {
      return std::make_unique<ClientRawConnection>(*this);
    }
    return std::make_unique<ClientSslConnection>(*this);
  }

  std::unique_ptr<TestClientConnection> createClientConnectionWithSni(absl::string_view sni) {
    if (context_ != nullptr) {
      extra_contexts_.push_back(std::move(context_));
    }
    ssl_options_.setSni(std::string(sni));
    context_ = Ssl::createClientSslTransportSocketFactory(ssl_options_, *context_manager_, *api_,
                                                          &server_factory_context_.serverScope());
    return createClientConnection();
  }

  // Configures tcp_proxy to wait for downstream TLS handshake completion before connecting
  // upstream so connections rejected during pending admission do not enqueue dead upstream sockets
  // on dataStream().
  void delayUpstreamConnectUntilTlsHandshake() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter = bootstrap.mutable_static_resources()
                         ->mutable_listeners(0)
                         ->mutable_filter_chains(0)
                         ->mutable_filters(0);
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
      std::ignore = filter->typed_config().UnpackTo(&tcp_proxy);
      tcp_proxy.set_upstream_connect_mode(
          envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_TLS_HANDSHAKE);
      tcp_proxy.mutable_max_early_data_bytes()->set_value(0);
      std::ignore = filter->mutable_typed_config()->PackFrom(tcp_proxy);
    });
  }

  std::string cacert() const { return upstream_selector_ ? "upstreamcacert" : "cacert"; }

protected:
  const bool upstream_selector_;
  bool mtls_{false};
  bool validation_sds_{false};
  bool use_dynamic_upstream_filter_state_{false};
  std::string filter_state_value_;
  // Certificate-level TLS parameters to attach to every certificate secret sent over SDS.
  std::optional<envoy::extensions::transport_sockets::tls::v3::TlsParameters> cert_tls_params_;
  std::shared_ptr<DynamicFilterState> dynamic_filter_state_;
  DynamicTenantListenerFilterFactory listener_tenant_factory_;
  Registry::InjectFactory<Server::Configuration::NamedListenerFilterConfigFactory>
      listener_tenant_reg_;
  DynamicUpstreamFilterStateNetworkFilterFactory upstream_filter_state_factory_;
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory>
      upstream_filter_state_reg_;
  std::vector<Network::UpstreamTransportSocketFactoryPtr> extra_contexts_;

  envoy::extensions::transport_sockets::tls::v3::Secret makeSecret(absl::string_view name,
                                                                   absl::string_view cert) {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(name);
    if (cert == "cacert" || cert == "upstreamcacert") {
      auto* validation_context = secret.mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(TestEnvironment::runfilesPath(
          absl::StrCat("test/config/integration/certs/", cert, ".pem")));
    } else {
      auto* tls_certificate = secret.mutable_tls_certificate();
      tls_certificate->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath(
          absl::StrCat("test/config/integration/certs/", cert, "cert.pem")));
      tls_certificate->mutable_private_key()->set_filename(TestEnvironment::runfilesPath(
          absl::StrCat("test/config/integration/certs/", cert, "key.pem")));
      if (cert_tls_params_.has_value()) {
        *tls_certificate->mutable_tls_params() = *cert_tls_params_;
      }
    }
    return secret;
  }

  FakeStream& waitForSdsRequest(absl::string_view name) {
    xds_streams_.emplace_back();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_streams_.back());
    RELEASE_ASSERT(result, result.message());
    auto& xds_stream = *xds_streams_.back();
    xds_stream.startGrpcStream();

    envoy::service::discovery::v3::DeltaDiscoveryRequest delta_discovery_request;
    AssertionResult result2 = xds_stream.waitForGrpcMessage(*dispatcher_, delta_discovery_request);
    RELEASE_ASSERT(result2, result2.message());
    EXPECT_EQ(1, delta_discovery_request.resource_names_subscribe().size())
        << "Should be 1 resource in DELTA_GRPC";
    EXPECT_EQ(name, delta_discovery_request.resource_names_subscribe().at(0))
        << "Secret name doesn't match in the request";
    return xds_stream;
  }

  FakeStream& waitSendSdsResponse(absl::string_view name, absl::string_view cert = "",
                                  bool fail_fast = false) {
    auto& xds_stream = waitForSdsRequest(name);
    if (fail_fast) {
      removeSecret(xds_stream, name);
      return xds_stream;
    }
    sendSecret(xds_stream, name, cert.empty() ? name : cert);
    return xds_stream;
  }

  void sendSecret(FakeStream& xds_stream, absl::string_view name, absl::string_view cert) {
    envoy::service::discovery::v3::DeltaDiscoveryResponse discovery_response;
    discovery_response.set_type_url(Config::TestTypeUrl::get().Secret);
    auto* resource = discovery_response.add_resources();
    resource->set_name(name);
    std::ignore = resource->mutable_resource()->PackFrom(makeSecret(name, cert));
    xds_stream.sendGrpcMessage(discovery_response);
  }

  void removeSecret(FakeStream& xds_stream, absl::string_view name) {
    envoy::service::discovery::v3::DeltaDiscoveryResponse discovery_response;
    discovery_response.set_type_url(Config::TestTypeUrl::get().Secret);
    discovery_response.add_removed_resources(name);
    xds_stream.sendGrpcMessage(discovery_response);
    xds_stream.finishGrpcStream(Grpc::Status::Ok);
  }

  void waitCertsRequested(uint32_t count) {
    test_server_->waitForCounter(onDemandStat("cert_requested"), Eq(count),
                                 TestUtility::DefaultTimeout, dispatcher_.get());
  }

  std::string onDemandStat(absl::string_view stat) {
    return upstream_selector_ ? absl::StrCat("cluster.cluster_0.on_demand_secret.", stat)
                              : listenerStatPrefix(absl::StrCat("on_demand_secret.", stat));
  }

  std::vector<FakeStreamPtr> xds_streams_;
};

TEST_P(OnDemandIntegrationTest, BasicSuccessWithPrefetch) {
  on_server_init_function_ = [&]() {
    createXdsConnection();
    waitSendSdsResponse("server");
  };
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
      name: server
  prefetch_secret_names:
  - server
  )EOF");
  // Open two connections sequentially.
  for (int i = 0; i < 2; i++) {
    auto conn = createClientConnection();
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_requested"))->value());
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
  test_server_->waitForCounter("sds.server.update_success", Eq(1));
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(1));
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessWithoutPrefetch) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
      name: server
  )EOF");
  {
    auto conn = createClientConnection();
    if (upstream_selector_) {
      conn->waitForUpstreamConnection();
    }
    waitCertsRequested(1);
    createXdsConnection();
    waitSendSdsResponse("server");
    if (!upstream_selector_) {
      conn->waitForUpstreamConnection();
    }
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }

  {
    // Open a second connection, without expecting SDS.
    auto conn2 = createClientConnection();
    conn2->waitForUpstreamConnection();
    conn2->sendAndReceiveTlsData("hello", "world");
    conn2.reset();
  }
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_requested"))->value());
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
  test_server_->waitForCounter("sds.server.update_success", Eq(1));
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(1));
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessSNI) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper only works on downstream";
  };
  ssl_options_.setSni("server");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  )EOF");
  auto conn = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
  test_server_->waitForCounter("sds.server.update_success", Eq(1));
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMixed) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
      name: server
  prefetch_secret_names:
  - server2
  )EOF");

  createXdsConnection();
  waitSendSdsResponse("server2");
  auto conn = createClientConnection();
  if (upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  waitCertsRequested(2);
  waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  test_server_->waitForCounter("sds.server.update_success", Eq(1));
  test_server_->waitForCounter("sds.server2.update_success", Eq(1));
  EXPECT_EQ(2, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicFail) {
  setup();
  auto conn = createClientConnection();
  if (upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server", "", true);
  conn->waitForDisconnect();
  test_server_->waitForGauge(onDemandStat("cert_active"), Eq(0));
}

TEST_P(OnDemandIntegrationTest, TwoPendingConnections) {
  setup();
  // Queue two connections in pending state.
  auto conn1 = createClientConnection();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  auto conn2 = createClientConnection();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  if (upstream_selector_) {
    conn1->waitForUpstreamConnection();
    conn2->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn1->waitForUpstreamConnection();
    conn2->waitForUpstreamConnection();
    conn1->sendAndReceiveTlsData("hello", "world");
    conn1.reset();
    conn2->sendAndReceiveTlsData("lorem", "ipsum");
  } else {
    conn1->close();
    conn2->close();
  }
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, ClientInterruptedHandshake) {
  setup();
  auto conn1 = createClientConnection();
  if (upstream_selector_) {
    conn1->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  // cert_active is incremented on the main thread right after cert_requested, so it may not be
  // visible yet when the counter wait returns: establish it before closing the client.
  test_server_->waitForGauge(onDemandStat("cert_active"), Eq(1));
  conn1->close();
  conn1.reset();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // SDS request is still outstanding, so we can respond to it, and it will be used later.
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
  createXdsConnection();
  waitSendSdsResponse("server");
}

TEST_P(OnDemandIntegrationTest, ListenerConnectTimeout) {
  if (upstream_selector_) {
    GTEST_SKIP() << "Upstream selector does not depend on listener socket timeout";
  }
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    auto* connect_timeout = filter_chain->mutable_transport_socket_connect_timeout();
    connect_timeout->set_seconds(1);
  });
  setup();
  auto conn = createClientConnection();
  test_server_->waitForCounter(listenerStatPrefix("downstream_cx_transport_socket_connect_timeout"),
                               Eq(1), TestUtility::DefaultTimeout, dispatcher_.get());
  conn->close();
  conn.reset();
  // SDS request is still outstanding, so we can respond to it, and it will be used later.
  createXdsConnection();
  waitSendSdsResponse("server");
}

TEST_P(OnDemandIntegrationTest, SecretAddRemove) {
  setup();
  // Add successfully.
  auto conn = createClientConnection();
  if (upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  createXdsConnection();
  auto& stream = waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());

  // Remove.
  removeSecret(stream, "server");
  test_server_->waitForGauge(onDemandStat("cert_active"), Eq(0));

  // Request again.
  auto conn2 = createClientConnection();
  if (upstream_selector_) {
    conn2->waitForUpstreamConnection();
  }
  waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn2->waitForUpstreamConnection();
  }
  conn2->sendAndReceiveTlsData("hello", "world");
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, SecretUpdate) {
  setup();
  auto conn1 = createClientConnection();
  if (upstream_selector_) {
    conn1->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  createXdsConnection();
  auto& stream = waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn1->waitForUpstreamConnection();
  }
  conn1->sendAndReceiveTlsData("hello", "world");
  conn1.reset();

  // Update with another valid secret.
  sendSecret(stream, "server", "server2");
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(2));

  auto conn2 = createClientConnection();
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  conn2.reset();

  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMtlsSuccess) {
  if (upstream_selector_) {
    GTEST_SKIP() << "mTLS only applies to downstream listener config";
  }
  mtls_ = true;
  setup();
  auto conn = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  // Ensure that the session ID is not issued: this is an indirect evidence that TLS resumption
  // is disabled on the server-side.
  EXPECT_EQ("", conn->tlsSessionId())
      << "Unexpected TLS session ID: " << conn->tlsSessionId().value_or("<none>");
  conn.reset();
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMtlsFail) {
  if (upstream_selector_) {
    GTEST_SKIP() << "mTLS only applies to downstream listener config";
  }
  mtls_ = true;
  ssl_options_.no_cert_ = true;
  useListenerAccessLog("%DOWNSTREAM_TRANSPORT_FAILURE_REASON%");
  setup();
  auto conn = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->waitForDisconnect();
  auto log_result = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_result, ::testing::HasSubstr("PEER_DID_NOT_RETURN_A_CERTIFICATE"));
}

TEST_P(OnDemandIntegrationTest, ValidationContextUpdate) {
  mtls_ = true;
  validation_sds_ = true;
  FakeStream* ca_stream = nullptr;
  on_server_init_function_ = [&]() {
    createXdsConnection();
    // This is a valid CA cert.
    ca_stream = &waitSendSdsResponse(cacert());
  };
  setup();
  // Connection should work as-expected.
  {
    auto conn = createClientConnection();
    if (upstream_selector_) {
      conn->waitForUpstreamConnection();
    }
    waitCertsRequested(1);
    waitSendSdsResponse("server");
    if (!upstream_selector_) {
      conn->waitForUpstreamConnection();
    }
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(1));

  // Send a wrong CA via validation SDS and open a new connection that fails.
  {
    sendSecret(*ca_stream, cacert(), upstream_selector_ ? "cacert" : "upstreamcacert");
    test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(2));
    auto conn = createClientConnection();
    if (upstream_selector_) {
      conn->waitForUpstreamConnection();
    }
    conn->waitForDisconnect();
    conn.reset();
  }
}

TEST_P(OnDemandIntegrationTest, ValidationContextUpdateWithPending) {
  if (upstream_selector_) {
    GTEST_SKIP() << "Cannot test because upstream selector validates with the CA prior to sending "
                    "a client cert";
  }
  mtls_ = true;
  validation_sds_ = true;
  FakeStream* ca_stream = nullptr;
  on_server_init_function_ = [&]() {
    createXdsConnection();
    // This is an invalid CA cert.
    ca_stream = &waitSendSdsResponse(cacert(), upstream_selector_ ? "cacert" : "upstreamcacert");
  };
  setup();
  // Queue a pending connection, then issue a context config update, and unblock the connection.
  // In this case, the original context might reference an older context config.
  auto conn = createClientConnection();
  waitCertsRequested(1);
  // Fix the CA cert, then send the actual server cert.
  sendSecret(*ca_stream, cacert(), cacert());
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
}

TEST_P(OnDemandIntegrationTest, BasicSuccessFilterStateOverride) {
  if (!upstream_selector_) {
    GTEST_SKIP() << "Filter state mapper only works on upstream";
  };
  filter_state_value_ = "server";
  setup(R"EOF(
  certificate_mapper:
    name: filter_state_override
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.filter_state_override.v3.Config
      default_value: "*"
  )EOF");
  auto conn = createClientConnection();
  conn->waitForUpstreamConnection();
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
}

// Exercises the RFC 8879 compressed-certificate cache together with the on-demand selector,
// verifying the correct certificate is served as the certificate behind a name changes.
//
// Each certificate gets its own SSL_CTX (the on-demand selector switches to it via
// SSL_set_SSL_CTX during the handshake) with its own compressed-cert cache, keyed by
// (algorithm, certificate chain). Two sequential connections per certificate exercise a
// cache miss then a cache hit; after the secret is updated to a different chain, the selector
// serves it from a new SSL_CTX. Each connection validates that the certificate presented to
// the client is the one currently configured -- never a stale cached chain.
TEST_P(OnDemandIntegrationTest, CertCompressionCacheAcrossContexts) {
  if (upstream_selector_) {
    GTEST_SKIP() << "Certificate compression applies to the downstream server certificate path";
  }
  config_helper_.addRuntimeOverride("envoy.reloadable_features.tls_certificate_compression_brotli",
                                    "true");
  setup();

  // sha256PeerCertificateDigest() is lowercase, separator-free hex; the cert hash headers are
  // uppercase and colon-separated.
  const auto normalize = [](absl::string_view hash) {
    return absl::AsciiStrToLower(absl::StrReplaceAll(hash, {{":", ""}}));
  };
  const std::string server_digest = normalize(TEST_SERVER_CERT_HASH);
  const std::string server2_digest = normalize(TEST_SERVER2_CERT_HASH);

  // First connection: cache miss compresses and stores the chain for "server".
  auto conn1 = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  auto& stream = waitSendSdsResponse("server");
  conn1->waitForUpstreamConnection();
  conn1->sendAndReceiveTlsData("hello", "world");
  EXPECT_EQ(server_digest, conn1->peerCertificateSha256Digest().value_or(""));
  conn1.reset();

  // Second connection on the same, unchanged certificate: cache hit on the same SSL_CTX.
  auto conn1b = createClientConnection();
  conn1b->waitForUpstreamConnection();
  conn1b->sendAndReceiveTlsData("hello", "world");
  EXPECT_EQ(server_digest, conn1b->peerCertificateSha256Digest().value_or(""));
  conn1b.reset();

  // Update "server" to a different certificate chain. The on-demand selector builds a new
  // SSL_CTX (with a fresh, empty compressed-cert cache) for it.
  sendSecret(stream, "server", "server2");
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(2));

  // Two more connections now serve the new chain: miss then hit on the new SSL_CTX. Each must
  // present the new certificate, proving no stale chain is served from a cache.
  for (int i = 0; i < 2; i++) {
    auto conn = createClientConnection();
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    EXPECT_EQ(server2_digest, conn->peerCertificateSha256Digest().value_or(""));
    conn.reset();
  }
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

// Certificate-level tls_params delivered over SDS take effect on the on-demand path. The parameters
// travel with the certificate config, so the context built for the secret carries them and the
// handshaker applies them to the connection once the pending selection completes.
//
// The certificate restricts the ciphers to AES256 while the client offers the default suites, which
// prefer AES128. The second connection repeats the handshake with the same certificate and no
// tls_params, confirming that AES256 came from the certificate and not from the client's offer.
TEST_P(OnDemandIntegrationTest, PerCertTlsParamsAppliedAfterAsyncSelection) {
  if (upstream_selector_) {
    GTEST_SKIP() << "Certificate-level tls_params are ignored on client certificates";
  }
  envoy::extensions::transport_sockets::tls::v3::TlsParameters params;
  params.add_cipher_suites("ECDHE-RSA-AES256-GCM-SHA384");
  cert_tls_params_ = params;
  // Cipher suites only apply to TLS 1.2.
  ssl_options_.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  setup();

  auto conn = createClientConnection();
  // The certificate is not available yet, so selection goes through the asynchronous path.
  waitCertsRequested(1);
  createXdsConnection();
  auto& stream = waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  EXPECT_EQ("ECDHE-RSA-AES256-GCM-SHA384", conn->ciphersuite().value_or(""));
  conn.reset();

  cert_tls_params_.reset();
  sendSecret(stream, "server", "server");
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(2));

  auto conn2 = createClientConnection();
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  EXPECT_EQ("ECDHE-RSA-AES128-GCM-SHA256", conn2->ciphersuite().value_or(""));
  conn2.reset();
}

TEST_P(OnDemandIntegrationTest, MaxActiveSecretsSieveEviction) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper only works on downstream";
  }
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  max_active_secrets: 2
  )EOF");

  // Load s1.
  {
    auto conn = createClientConnectionWithSni("s1");
    waitCertsRequested(1);
    createXdsConnection();
    waitSendSdsResponse("s1", "server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(1));

  // Load s2 (cache now at capacity 2).
  {
    auto conn = createClientConnectionWithSni("s2");
    waitCertsRequested(2);
    waitSendSdsResponse("s2", "server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(2));
  EXPECT_EQ(2, test_server_->gauge(onDemandStat("cert_active"))->value());

  // Hit s1 again (cache hit, marks s1 visited so SIEVE spares it).
  {
    auto conn = createClientConnectionWithSni("s1");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  EXPECT_EQ(2, test_server_->counter(onDemandStat("cert_requested"))->value());

  // Load s3 -> SIEVE skips visited s1 and evicts unvisited s2.
  {
    auto conn = createClientConnectionWithSni("s3");
    waitCertsRequested(3);
    waitSendSdsResponse("s3", "server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(3));
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_evicted"))->value());
  EXPECT_EQ(2, test_server_->gauge(onDemandStat("cert_active"))->value());

  // s1 must still be cached (no SDS request).
  {
    auto conn = createClientConnectionWithSni("s1");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  EXPECT_EQ(3, test_server_->counter(onDemandStat("cert_requested"))->value());

  // s2 was evicted and must trigger a fresh SDS fetch.
  {
    auto conn = createClientConnectionWithSni("s2");
    waitCertsRequested(4);
    waitSendSdsResponse("s2", "server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(4));
  EXPECT_EQ(2, test_server_->counter(onDemandStat("cert_evicted"))->value());
  EXPECT_EQ(2, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, MaxActiveSecretsPerTenantFairEviction) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper and listener filter only apply to downstream";
  }
  config_helper_.addListenerFilter(R"EOF(
  name: test.filters.listener.dynamic_tenant
  typed_config:
    "@type": type.googleapis.com/google.protobuf.Struct
  )EOF");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  max_active_secrets: 2
  )EOF");

  // Tenant B loads b1 (and never hits it again during A's flood).
  dynamic_filter_state_->setTenant("tenant-b");
  {
    auto conn = createClientConnectionWithSni("b1");
    waitCertsRequested(1);
    createXdsConnection();
    waitSendSdsResponse("b1", "server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(1));

  // Tenant A loads a1, a2, a3: fair eviction must evict Tenant A's own certs while preserving b1.
  dynamic_filter_state_->setTenant("tenant-a");
  uint32_t expected_reqs = 1;
  for (const auto& sni : {"a1", "a2", "a3"}) {
    auto conn = createClientConnectionWithSni(sni);
    waitCertsRequested(++expected_reqs);
    waitSendSdsResponse(sni, "server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(expected_reqs));
  }
  EXPECT_EQ(2, test_server_->counter(onDemandStat("cert_evicted"))->value());
  EXPECT_EQ(2, test_server_->gauge(onDemandStat("cert_active"))->value());

  // Tenant B's b1 must still be cached (hit without SDS).
  dynamic_filter_state_->setTenant("tenant-b");
  {
    auto conn = createClientConnectionWithSni("b1");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  EXPECT_EQ(expected_reqs, test_server_->counter(onDemandStat("cert_requested"))->value());
}

TEST_P(OnDemandIntegrationTest, MaxActiveSecretsLazyOwnershipTransfer) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper and listener filter only apply to downstream";
  }
  config_helper_.addListenerFilter(R"EOF(
  name: test.filters.listener.dynamic_tenant
  typed_config:
    "@type": type.googleapis.com/google.protobuf.Struct
  )EOF");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  max_active_secrets: 3
  )EOF");

  // Tenant A initially mints shared, a1, a2 (filling all 3 slots).
  dynamic_filter_state_->setTenant("tenant-a");
  {
    auto conn = createClientConnectionWithSni("shared");
    waitCertsRequested(1);
    createXdsConnection();
    waitSendSdsResponse("shared", "server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(1));

  uint32_t expected_reqs = 1;
  for (const auto& sni : {"a1", "a2"}) {
    auto conn = createClientConnectionWithSni(sni);
    waitCertsRequested(++expected_reqs);
    waitSendSdsResponse(sni, "server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(expected_reqs));
  }

  // Tenant B (0 active certs) hits "shared", updating last_hit_tenant to tenant-b.
  dynamic_filter_state_->setTenant("tenant-b");
  {
    auto conn = createClientConnectionWithSni("shared");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  EXPECT_EQ(3, test_server_->counter(onDemandStat("cert_requested"))->value());

  // Tenant A floods a3: lazy ownership transfer moves "shared" to tenant-b and evicts a1 instead.
  dynamic_filter_state_->setTenant("tenant-a");
  {
    auto conn = createClientConnectionWithSni("a3");
    waitCertsRequested(++expected_reqs);
    waitSendSdsResponse("a3", "server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(expected_reqs));
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_evicted"))->value());
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_ownership_transferred"))->value());
  EXPECT_EQ(3, test_server_->gauge(onDemandStat("cert_active"))->value());

  // "shared" must still be cached (hit without SDS).
  dynamic_filter_state_->setTenant("tenant-b");
  {
    auto conn = createClientConnectionWithSni("shared");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  EXPECT_EQ(expected_reqs, test_server_->counter(onDemandStat("cert_requested"))->value());
}

TEST_P(OnDemandIntegrationTest, MaxActiveSecretsUpstreamFilterStateTenant) {
  if (!upstream_selector_) {
    GTEST_SKIP() << "Upstream shared filter state test only applies to upstream selector";
  }
  use_dynamic_upstream_filter_state_ = true;
  setup(R"EOF(
  certificate_mapper:
    name: filter_state_override
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.filter_state_override.v3.Config
      default_value: "*"
  max_active_secrets: 2
  )EOF");

  // Tenant B loads b1.
  dynamic_filter_state_->setUpstream("b1", "tenant-b");
  {
    auto conn = createClientConnection();
    conn->waitForUpstreamConnection();
    waitCertsRequested(1);
    createXdsConnection();
    waitSendSdsResponse("b1", "server");
    conn->sendAndReceiveTlsData("hello", "world");
  }
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(1));

  // Tenant A loads a1, then a2 -> evicts a1 while preserving b1.
  uint32_t expected_reqs = 1;
  for (const auto& secret : {"a1", "a2"}) {
    dynamic_filter_state_->setUpstream(secret, "tenant-a");
    auto conn = createClientConnection();
    conn->waitForUpstreamConnection();
    waitCertsRequested(++expected_reqs);
    waitSendSdsResponse(secret, "server");
    conn->sendAndReceiveTlsData("hello", "world");
    test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(expected_reqs));
  }
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_evicted"))->value());
  EXPECT_EQ(2, test_server_->gauge(onDemandStat("cert_active"))->value());

  // Tenant B hits b1 without triggering an SDS fetch.
  dynamic_filter_state_->setUpstream("b1", "tenant-b");
  {
    auto conn = createClientConnection();
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
  }
  EXPECT_EQ(expected_reqs, test_server_->counter(onDemandStat("cert_requested"))->value());
}

TEST_P(OnDemandIntegrationTest, MaxPendingSecretsRejection) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper only works on downstream";
  }
  delayUpstreamConnectUntilTlsHandshake();
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  max_pending_secrets: 1
  )EOF");

  // Start pending fetch for s1 and hold the SDS response.
  auto conn1 = createClientConnectionWithSni("s1");
  waitCertsRequested(1);
  createXdsConnection();
  auto& s1_stream = waitForSdsRequest("s1");
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_pending"))->value());

  // A duplicate handshake for s1 coalesces without consuming another pending slot.
  auto conn1_dup = createClientConnectionWithSni("s1");
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // A distinct secret s2 is rejected while s1 occupies the single pending slot.
  auto conn2 = createClientConnectionWithSni("s2");
  test_server_->waitForCounter(onDemandStat("cert_pending_rejected"), Eq(1),
                               TestUtility::DefaultTimeout, dispatcher_.get());
  conn2->waitForDisconnect();
  conn2.reset();
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_pending"))->value());

  // Complete s1: both coalesced connections complete TLS handshakes and open upstream connections,
  // and pending count returns to 0.
  sendSecret(s1_stream, "s1", "server");
  conn1->waitForUpstreamConnection();
  conn1_dup->waitForUpstreamConnection();
  conn1->close();
  conn1.reset();
  conn1_dup->close();
  conn1_dup.reset();
  test_server_->waitForGauge(onDemandStat("cert_pending"), Eq(0));

  // Now s2 is admitted and succeeds.
  {
    auto conn3 = createClientConnectionWithSni("s2");
    waitCertsRequested(2);
    waitSendSdsResponse("s2", "server");
    conn3->waitForUpstreamConnection();
    conn3->sendAndReceiveTlsData("hello", "world");
  }
  test_server_->waitForGauge(onDemandStat("cert_pending"), Eq(0));
}

TEST_P(OnDemandIntegrationTest, MaxPendingSecretsMultiTenantDynamicThreshold) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper and listener filter only apply to downstream";
  }
  delayUpstreamConnectUntilTlsHandshake();
  config_helper_.addListenerFilter(R"EOF(
  name: test.filters.listener.dynamic_tenant
  typed_config:
    "@type": type.googleapis.com/google.protobuf.Struct
  )EOF");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  max_pending_secrets: 2
  )EOF");

  // Tenant A opens pending a1 (admitted: 0 < 2).
  dynamic_filter_state_->setTenant("tenant-a");
  auto conn_a1 = createClientConnectionWithSni("a1");
  waitCertsRequested(1);
  createXdsConnection();
  auto& a1_stream = waitForSdsRequest("a1");
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_pending"))->value());

  // Tenant A attempts a2 -> rejected by Choudhury & Hahne Dynamic Thresholds (1 < 2 - 1 is false),
  // reserving the remaining pending slot for other tenants.
  auto conn_a2 = createClientConnectionWithSni("a2");
  test_server_->waitForCounter(onDemandStat("cert_pending_rejected"), Eq(1),
                               TestUtility::DefaultTimeout, dispatcher_.get());
  conn_a2->waitForDisconnect();
  conn_a2.reset();

  // Tenant B opens pending b1 -> admitted into the reserved slot.
  dynamic_filter_state_->setTenant("tenant-b");
  auto conn_b1 = createClientConnectionWithSni("b1");
  waitCertsRequested(2);
  auto& b1_stream = waitForSdsRequest("b1");
  EXPECT_EQ(2, test_server_->gauge(onDemandStat("cert_pending"))->value());

  // Complete a1 first, then b1, so each connection deterministically pairs with its own upstream
  // socket on dataStream().
  sendSecret(a1_stream, "a1", "server");
  conn_a1->waitForUpstreamConnection();
  conn_a1->sendAndReceiveTlsData("hello", "world");
  conn_a1.reset();
  test_server_->waitForGauge(onDemandStat("cert_pending"), Eq(1));

  sendSecret(b1_stream, "b1", "server");
  conn_b1->waitForUpstreamConnection();
  conn_b1->sendAndReceiveTlsData("lorem", "ipsum");
  conn_b1.reset();
  test_server_->waitForGauge(onDemandStat("cert_pending"), Eq(0));
}

TEST_P(OnDemandIntegrationTest, ListenerSetFilterStateTenantIntegration) {
  if (upstream_selector_) {
    GTEST_SKIP() << "Listener set_filter_state only applies to downstream";
  }
  delayUpstreamConnectUntilTlsHandshake();
  // Verify the real envoy.filters.listener.set_filter_state extension wires the fixed
  // `envoy.tls.certificate_selectors.on_demand_secret.tenant_id` filter state key into the
  // on-demand selector (proving named-tenant dynamic threshold caps a single tenant to 1 of 2
  // pending slots).
  config_helper_.addListenerFilter(R"EOF(
  name: envoy.filters.listener.set_filter_state
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.listener.set_filter_state.v3.Config
    on_accept:
    - object_key: envoy.tls.certificate_selectors.on_demand_secret.tenant_id
      factory_key: envoy.string
      format_string:
        text_format_source:
          inline_string: tenant-from-listener-filter
  )EOF");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  max_pending_secrets: 2
  )EOF");

  auto conn1 = createClientConnectionWithSni("s1");
  waitCertsRequested(1);
  createXdsConnection();
  auto& s1_stream = waitForSdsRequest("s1");

  // Because a named tenant was extracted from envoy.filters.listener.set_filter_state, the second
  // pending secret from the same tenant is rejected by dynamic thresholds (whereas unattributed
  // default-bucket mode would have admitted both slots).
  auto conn2 = createClientConnectionWithSni("s2");
  test_server_->waitForCounter(onDemandStat("cert_pending_rejected"), Eq(1),
                               TestUtility::DefaultTimeout, dispatcher_.get());
  conn2->waitForDisconnect();
  conn2.reset();

  sendSecret(s1_stream, "s1", "server");
  conn1->waitForUpstreamConnection();
  conn1->sendAndReceiveTlsData("hello", "world");
  conn1.reset();
  test_server_->waitForGauge(onDemandStat("cert_pending"), Eq(0));
}

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, OnDemandIntegrationTest,
                         testing::ValuesIn(testParams()), testParamsToString);

} // namespace
} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
