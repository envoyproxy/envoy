#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

constexpr char SDS_CLUSTER_NAME[] = "sds_cluster.lyft.com";
constexpr char RTDS_CLUSTER_NAME[] = "rtds_cluster";
constexpr char CLIENT_CERT_NAME[] = "client_cert";

std::string kvStoreDelegateConfig() {
  const std::string filename = TestEnvironment::temporaryPath("xds_kv_store.txt");
  ::unlink(filename.c_str());

  return fmt::format(R"EOF(
    name: envoy.config.xds.KeyValueStoreXdsDelegate
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.xds.v3alpha.KeyValueStoreXdsDelegateConfig
      key_value_store_config:
        config:
          name: envoy.key_value.file_based
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.key_value.file_based.v3.FileBasedKeyValueStoreConfig
            filename: {}
    )EOF",
                     filename);
}

class KeyValueStoreXdsDelegateIntegrationTest : public HttpIntegrationTest,
                                                public Grpc::GrpcClientIntegrationParamTest {
public:
  KeyValueStoreXdsDelegateIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::baseConfigNoListeners()) {
    use_lds_ = false;
    // TODO(abeyad): add UnifiedSotw tests too when implementation is ready.
    sotw_or_delta_ = Grpc::SotwOrDelta::Sotw;
    skip_tag_extraction_rule_check_ = true;

    // Make the default cluster HTTP2.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ConfigHelper::setHttp2(*bootstrap.mutable_static_resources()->mutable_clusters(0));
    });

    // Add xDS clusters.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add the SDS cluster.
      addXdsCluster(bootstrap, std::string(SDS_CLUSTER_NAME));
      // Add the RTDS cluster.
      addXdsCluster(bootstrap, std::string(RTDS_CLUSTER_NAME));
    });

    // Set up the initial static cluster with SSL using SDS.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
      tls_context.set_sni("lyft.com");
      auto* secret_config =
          tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
      setUpSdsConfig(secret_config, CLIENT_CERT_NAME);
      transport_socket->set_name("envoy.transport_sockets.tls");
      transport_socket->mutable_typed_config()->PackFrom(tls_context);
    });

    // Add static runtime values.
    config_helper_.addRuntimeOverride("foo", "whatevs");
    config_helper_.addRuntimeOverride("bar", "yar");

    // Set up the RTDS runtime layer.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* layer = bootstrap.mutable_layered_runtime()->add_layers();
      layer->set_name("some_rtds_layer");
      auto* rtds_layer = layer->mutable_rtds_layer();
      rtds_layer->set_name("some_rtds_layer");
      auto* rtds_config = rtds_layer->mutable_rtds_config();
      rtds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* api_config_source = rtds_config->mutable_api_config_source();
      api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      api_config_source->set_set_node_on_first_message_only(true);
      api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
          RTDS_CLUSTER_NAME);
    });

    // Add test xDS delegate.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* delegate_extension = bootstrap.mutable_xds_delegate_extension();
      TestUtility::loadFromYaml(kvStoreDelegateConfig(), *delegate_extension);
    });
  }

  void initialize() override {
    HttpIntegrationTest::initialize();
    // Register admin port.
    registerTestServerPorts({});
  }

  void TearDown() override {
    closeConnection(sds_connection_);
    closeConnection(rtds_connection_);
    cleanupUpstreamAndDownstream();
    codec_client_.reset();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void createUpstreams() override {
    // Static cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
    // SDS Cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
    // RTDS Cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
  }

protected:
  FakeUpstream& getSdsUpstream() { return *fake_upstreams_[1]; }
  FakeUpstream& getRtdsUpstream() { return *fake_upstreams_[2]; }

  void addXdsCluster(envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                     const std::string& cluster_name) {
    auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
    xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    xds_cluster->set_name(cluster_name);
    xds_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
    ConfigHelper::setHttp2(*xds_cluster);
  }

  void initXdsStream(FakeUpstream& upstream, FakeHttpConnectionPtr& connection,
                     FakeStreamPtr& stream) {
    AssertionResult result = upstream.waitForHttpConnection(*dispatcher_, connection);
    RELEASE_ASSERT(result, result.message());
    result = connection->waitForNewStream(*dispatcher_, stream);
    RELEASE_ASSERT(result, result.message());
    stream->startGrpcStream();
  }

  void closeConnection(FakeHttpConnectionPtr& connection) {
    AssertionResult result = connection->close();
    RELEASE_ASSERT(result, result.message());
    result = connection->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
    connection.reset();
  }

  void setUpSdsConfig(envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig* secret_config,
                      const std::string& secret_name) {
    secret_config->set_name(secret_name);
    auto* config_source = secret_config->mutable_sds_config();
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    auto* grpc_service = api_config_source->add_grpc_services();
    setGrpcService(*grpc_service, SDS_CLUSTER_NAME, getSdsUpstream().localAddress());
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getClientSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(std::string(CLIENT_CERT_NAME));
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
    return secret;
  }

  std::string getRuntimeKey(const std::string& key) {
    auto response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "GET", "/runtime?format=json", "", downstreamProtocol(), version_);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(response->body());
    auto entries = loader->getObject("entries");
    if (entries->hasObject(key)) {
      return entries->getObject(key)->getString("final_value");
    }
    return "";
  }

  void checkSecretExists(const std::string& secret_name, const std::string& version_info) {
    auto response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "GET", "/config_dump?resource=dynamic_active_secrets", "",
        downstreamProtocol(), version_);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(response->body());
    envoy::admin::v3::ConfigDump config_dump;
    TestUtility::loadFromJson(loader->asJsonString(), config_dump);
    // Expect at least the "client_cert" dynamic secret.
    ASSERT_GE(config_dump.configs_size(), 1);
    envoy::admin::v3::SecretsConfigDump::DynamicSecret dynamic_secret;
    ASSERT_OK(MessageUtil::unpackToNoThrow(config_dump.configs(0), dynamic_secret));
    EXPECT_EQ(secret_name, dynamic_secret.name());
    EXPECT_EQ(version_info, dynamic_secret.version_info());
  }

  void shutdownAndRestartTestServer() {
    // Reset the test server.
    on_server_init_function_ = nullptr;
    test_server_.reset();

    // Set up a new Envoy, using the previous Envoy's configuration, and create the test server.
    ConfigHelper helper(version_, *api_,
                        MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
    std::vector<uint32_t> ports;
    std::vector<uint32_t> zero;
    for (auto& upstream : fake_upstreams_) {
      if (upstream->localAddress()->ip()) {
        ports.push_back(upstream->localAddress()->ip()->port());
        zero.push_back(0);
      }
    }
    helper.setPorts(zero, true); // Zero out ports set by config_helper_'s finalize();
    const std::string bootstrap_path = finalizeConfigWithPorts(helper, ports, use_lds_);

    std::vector<std::string> named_ports;
    const auto& static_resources = config_helper_.bootstrap().static_resources();
    named_ports.reserve(static_resources.listeners_size());
    for (int i = 0; i < static_resources.listeners_size(); ++i) {
      named_ports.push_back(static_resources.listeners(i).name());
    }
    createGeneratedApiTestServer(bootstrap_path, named_ports, {false, true, false}, false,
                                 test_server_);
    registerTestServerPorts(named_ports, test_server_);
  }

  FakeHttpConnectionPtr sds_connection_;
  FakeStreamPtr sds_stream_;
  FakeHttpConnectionPtr rtds_connection_;
  FakeStreamPtr rtds_stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, KeyValueStoreXdsDelegateIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(KeyValueStoreXdsDelegateIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [this]() {
    {
      // SDS.
      initXdsStream(getSdsUpstream(), sds_connection_, sds_stream_);
      EXPECT_TRUE(compareSotwDiscoveryRequest(
          /*expected_type_url=*/Config::TypeUrl::get().Secret, /*expected_version=*/"",
          /*expected_resource_names=*/{std::string(CLIENT_CERT_NAME)}, /*expect_node=*/true,
          /*expected_error_code=*/Grpc::Status::WellKnownGrpcStatus::Ok,
          /*expected_error_message=*/"", sds_stream_.get()));
      auto sds_resource = getClientSecret();
      sendSotwDiscoveryResponse<envoy::extensions::transport_sockets::tls::v3::Secret>(
          Config::TypeUrl::get().Secret, {sds_resource}, "1", sds_stream_.get());
    }
    {
      // RTDS.
      initXdsStream(getRtdsUpstream(), rtds_connection_, rtds_stream_);
      EXPECT_TRUE(compareSotwDiscoveryRequest(
          /*expected_type_url=*/Config::TypeUrl::get().Runtime,
          /*expected_version=*/"",
          /*expected_resource_names=*/{"some_rtds_layer"}, /*expect_node=*/true,
          /*expected_error_code=*/Grpc::Status::WellKnownGrpcStatus::Ok,
          /*expected_error_message=*/"", rtds_stream_.get()));
      auto rtds_resource = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
          name: some_rtds_layer
          layer:
            foo: bar
            baz: meh
      )EOF");
      sendSotwDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
          Config::TypeUrl::get().Runtime, {rtds_resource}, "1", rtds_stream_.get());
    }
  };

  initialize();

  // Wait until the discovery responses have been processed.
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);
  test_server_->waitForCounterGe("runtime.load_success", 2);

  // Verify that the xDS resources are used by Envoy.
  checkSecretExists(std::string(CLIENT_CERT_NAME), /*version_info=*/"1");
  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));

  // Send an update to the RTDS resource, from the RTDS cluster to the Envoy test server.
  EXPECT_TRUE(compareSotwDiscoveryRequest(
      /*expected_type_url=*/Config::TypeUrl::get().Runtime, /*expected_version=*/"1",
      /*expected_resource_names=*/{"some_rtds_layer"}, /*expect_node=*/false,
      /*expected_error_code=*/Grpc::Status::WellKnownGrpcStatus::Ok,
      /*expected_error_message=*/"", rtds_stream_.get()));
  auto rtds_resource = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      baz: saz
  )EOF");
  sendSotwDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TypeUrl::get().Runtime, {rtds_resource}, "2", rtds_stream_.get());
  test_server_->waitForCounterGe("runtime.load_success", 3);

  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("saz", getRuntimeKey("baz"));

  // Kill the current test server, and restart it using the same configuration.
  shutdownAndRestartTestServer();

  // Wait until SDS and RTDS have been loaded from disk and updated the Envoy instance.
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);
  test_server_->waitForCounterGe("runtime.load_success", 2);

  // Verify that the latest resource values are used by Envoy.
  checkSecretExists(std::string(CLIENT_CERT_NAME), /*version_info=*/"1");
  EXPECT_EQ("whatevs", getRuntimeKey("foo"));
  EXPECT_EQ("yar", getRuntimeKey("bar"));
  EXPECT_EQ("saz", getRuntimeKey("baz"));
}

} // namespace
} // namespace Envoy
