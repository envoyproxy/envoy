#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.validate.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

#include "source/common/config/api_version.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/utility.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/client_connection_factory_impl.h"
#endif

#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/config/integration/certs/servercert_info.h"
#include "test/config/integration/certs/server2cert_info.h"
#include "test/common/tls/test_private_key_method_provider.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/integration/ssl_utility.h"
#include "test/mocks/secret/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/resources.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "integration.h"
#include "utility.h"

namespace Envoy {
namespace Ssl {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
const envoy::service::secret::v3::SdsDummy _sds_dummy;

struct TestParams {
  Network::Address::IpVersion ip_version;
  Grpc::ClientType sds_grpc_type;
  bool test_quic;
};

std::string sdsTestParamsToString(const ::testing::TestParamInfo<TestParams>& p) {
  return fmt::format("{}_{}_{}", TestUtility::ipVersionToString(p.param.ip_version),
                     p.param.sds_grpc_type == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc"
                                                                           : "EnvoyGrpc",
                     p.param.test_quic ? "UsesQuic" : "UsesTcp");
}

std::vector<TestParams> getSdsTestsParams(bool disable_quic = false) {
  std::vector<TestParams> ret;
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    for (auto sds_grpc_type : TestEnvironment::getsGrpcVersionsForTest()) {
      ret.push_back(TestParams{ip_version, sds_grpc_type, false});
#ifdef ENVOY_ENABLE_QUIC
      if (!disable_quic) {
        ret.push_back(TestParams{ip_version, sds_grpc_type, true});
      }
#else
      UNREFERENCED_PARAMETER(disable_quic);
      ENVOY_LOG_MISC(warn, "Skipping HTTP/3 as support is compiled out");
#endif
    }
  }
  return ret;
}

// Sds integration base class with following support:
// * functions to create sds upstream, and send sds response
// * functions to create secret protobuf.
class SdsDynamicIntegrationBaseTest : public Grpc::BaseGrpcClientIntegrationParamTest,
                                      public HttpIntegrationTest,
                                      public testing::TestWithParam<TestParams> {
public:
  SdsDynamicIntegrationBaseTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam().ip_version),
        test_quic_(GetParam().test_quic) {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat
    // 'sds.client_cert.grpc.sds_cluster.lyft.com.streams_closed_12' and stat_prefix
    // 'sds_cluster.lyft.com'.
    skip_tag_extraction_rule_check_ = true;
  }

  SdsDynamicIntegrationBaseTest(Http::CodecType downstream_protocol,
                                Network::Address::IpVersion version, const std::string& config)
      : HttpIntegrationTest(downstream_protocol, version, config),
        test_quic_(GetParam().test_quic) {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat
    // 'sds.client_cert.grpc.sds_cluster.lyft.com.streams_closed_12' and stat_prefix
    // 'sds_cluster.lyft.com'.
    skip_tag_extraction_rule_check_ = true;
  }

  Network::Address::IpVersion ipVersion() const override { return GetParam().ip_version; }
  Grpc::ClientType clientType() const override { return GetParam().sds_grpc_type; }
  virtual std::unique_ptr<FakeUpstream>& sdsUpstream() { return fake_upstreams_[0]; }
  // Index in fake_upstreams_ vector of the data plane upstream.
  int dataPlaneUpstreamIndex() { return 1; }

protected:
  void createSdsStream(FakeUpstream&) {
    createXdsConnection();

    AssertionResult result2 = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result2, result2.message());
    xds_stream_->startGrpcStream();
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
    setGrpcService(*grpc_service, "sds_cluster.lyft.com", sdsUpstream()->localAddress());
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getServerSecretRsa() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(server_cert_rsa_);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
    return secret;
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getCvcSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(validation_secret_);
    auto* validation_context = secret.mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);
    return secret;
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getCvcSecretWithOnlyTrustedCa() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(validation_secret_);
    auto* validation_context = secret.mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    return secret;
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getClientSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(client_cert_);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
    return secret;
  }

  envoy::extensions::transport_sockets::tls::v3::Secret
  getWrongSecret(const std::string& secret_name) {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(secret_name);
    secret.mutable_tls_certificate();
    return secret;
  }

  void sendSdsResponse(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) {
    envoy::service::discovery::v3::DiscoveryResponse discovery_response;
    discovery_response.set_version_info("1");
    discovery_response.set_type_url(Config::TypeUrl::get().Secret);
    discovery_response.add_resources()->PackFrom(secret);

    xds_stream_->sendGrpcMessage(discovery_response);
  }

  void printServerCounters() {
    std::cerr << "all counters" << std::endl;
    for (const auto& c : test_server_->counters()) {
      std::cerr << "counter: " << c->name() << ", value: " << c->value() << std::endl;
    }
  }

  const std::string server_cert_rsa_{"server_cert_rsa"};
  const std::string server2_cert_rsa_{"server2_cert_rsa"};
  const std::string server_cert_ecdsa_{"server_cert_ecdsa"};
  const std::string validation_secret_{"validation_secret"};
  const std::string client_cert_{"client_cert"};
  bool v3_resource_api_{false};
  bool test_quic_;
};

// Downstream SDS integration test: static Listener with ssl cert from SDS
class SdsDynamicDownstreamIntegrationTest : public SdsDynamicIntegrationBaseTest {
public:
  SdsDynamicDownstreamIntegrationTest()
      : SdsDynamicIntegrationBaseTest(
            (GetParam().test_quic ? Http::CodecType::HTTP3 : Http::CodecType::HTTP1),
            GetParam().ip_version, ConfigHelper::httpProxyConfig(GetParam().test_quic)) {}

  void initialize() override {
    ASSERT(test_quic_ ? downstream_protocol_ == Http::CodecType::HTTP3
                      : downstream_protocol_ == Http::CodecType::HTTP1);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      config_helper_.configDownstreamTransportSocketWithTls(
          bootstrap,
          [this](
              envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
            configToUseSds(common_tls_context);
          });
      // The SNI of the certificates loaded in this test.
      default_request_headers_.setHost("www.lyft.com");
    });

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add a static SDS cluster as the first cluster in the list.
      // The SDS cluster needs to appear before the cluster that uses it for secrets, so that it
      // gets initialized first.
      bootstrap.mutable_static_resources()->add_clusters()->MergeFrom(
          bootstrap.static_resources().clusters(0));
      auto* sds_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      sds_cluster->set_name("sds_cluster.lyft.com");
      sds_cluster->mutable_load_assignment()->set_cluster_name("sds_cluster.lyft.com");
      ConfigHelper::setHttp2(*sds_cluster);
    });

    HttpIntegrationTest::initialize();
    client_ssl_ctx_ = createClientSslTransportSocketFactory({}, context_manager_, *api_);
  }

  void configToUseSds(
      envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
    common_tls_context.add_alpn_protocols(test_quic_ ? Http::Utility::AlpnNames::get().Http3
                                                     : Http::Utility::AlpnNames::get().Http11);

    auto* validation_context = common_tls_context.mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);

    // Modify the listener ssl cert to use SDS from sds_cluster
    auto* secret_config_rsa = common_tls_context.add_tls_certificate_sds_secret_configs();
    setUpSdsConfig(secret_config_rsa, server_cert_rsa_);

    // Add an additional SDS config for an EC cert (the base test has SDS config for an RSA cert).
    // This is done via the filesystem instead of gRPC to simplify the test setup.
    if (dual_cert_ || multi_cert_) {
      auto* secret_config_ecdsa = common_tls_context.add_tls_certificate_sds_secret_configs();

      secret_config_ecdsa->set_name(server_cert_ecdsa_);
      auto* config_source = secret_config_ecdsa->mutable_sds_config();
      constexpr absl::string_view sds_template =
          R"EOF(
---
version_info: "0"
resources:
- "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
  name: "{}"
  tls_certificate:
    certificate_chain:
      filename: "{}"
    private_key:
      filename: "{}"
)EOF";

      const std::string sds_content = fmt::format(
          sds_template, server_cert_ecdsa_,
          TestEnvironment::runfilesPath("test/config/integration/certs/server_ecdsacert.pem"),
          TestEnvironment::runfilesPath("test/config/integration/certs/server_ecdsakey.pem"));

      auto sds_path =
          TestEnvironment::writeStringToFileForTest("server_cert_ecdsa.sds.yaml", sds_content);
      config_source->mutable_path_config_source()->set_path(sds_path);
      config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    }

    // Add one more additional SDS config to test multiple(>2) certs.
    // This is done via the filesystem instead of gRPC to simplify the test setup.
    if (multi_cert_) {
      auto* secret_config_rsa_2 = common_tls_context.add_tls_certificate_sds_secret_configs();

      secret_config_rsa_2->set_name(server2_cert_rsa_);
      auto* config_source = secret_config_rsa_2->mutable_sds_config();
      constexpr absl::string_view sds_template =
          R"EOF(
---
version_info: "0"
resources:
- "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
  name: "{}"
  tls_certificate:
    certificate_chain:
      filename: "{}"
    private_key:
      filename: "{}"
)EOF";

      const std::string sds_content = fmt::format(
          sds_template, server2_cert_rsa_,
          TestEnvironment::runfilesPath("test/config/integration/certs/server2cert.pem"),
          TestEnvironment::runfilesPath("test/config/integration/certs/server2key.pem"));

      auto sds_path =
          TestEnvironment::writeStringToFileForTest("server2_cert_rsa.sds.yaml", sds_content);
      config_source->mutable_path_config_source()->set_path(sds_path);
      config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    }
  }

  void createUpstreams() override {
    // SDS cluster is H2, while the data cluster is H1.
    addFakeUpstream(Http::CodecType::HTTP2);
    addFakeUpstream(Http::CodecType::HTTP1);
    xds_upstream_ = fake_upstreams_.front().get();
  }

  void waitForSdsUpdateStats(size_t times) {
    test_server_->waitForCounterGe(
        listenerStatPrefix(test_quic_
                               ? "quic_server_transport_socket_factory.context_config_update_by_sds"
                               : "server_ssl_socket_factory.ssl_context_update_by_sds"),
        times, std::chrono::milliseconds(5000));
  }

  void TearDown() override {
    cleanUpXdsConnection();
    client_ssl_ctx_.reset();
  }

  Network::ClientConnectionPtr makeSslClientConnection() {
    int port = lookupPort("http");
    if (downstream_protocol_ <= Http::CodecType::HTTP2) {
      Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, port);
      return dispatcher_->createClientConnection(
          address, Network::Address::InstanceConstSharedPtr(),
          client_ssl_ctx_->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
    }
    return makeClientConnectionWithOptions(port, nullptr);
  }

protected:
  Network::UpstreamTransportSocketFactoryPtr client_ssl_ctx_;
  bool dual_cert_{false};
  bool multi_cert_{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, SdsDynamicDownstreamIntegrationTest,
                         testing::ValuesIn(getSdsTestsParams()), sdsTestParamsToString);

class SdsDynamicKeyRotationIntegrationTest : public SdsDynamicDownstreamIntegrationTest {
protected:
  envoy::extensions::transport_sockets::tls::v3::Secret getCurrentServerSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(server_cert_rsa_);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::temporaryPath("root/current/servercert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::temporaryPath("root/current/serverkey.pem"));
    auto* watched_directory = tls_certificate->mutable_watched_directory();
    watched_directory->set_path(TestEnvironment::temporaryPath("root"));
    return secret;
  }
};

// We don't care about multiple gRPC types here, Envoy gRPC is fine, the
// interest is on the filesystem.
INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, SdsDynamicKeyRotationIntegrationTest,
                         testing::ValuesIn(getSdsTestsParams()), sdsTestParamsToString);

// Validate that a basic key-cert rotation works via symlink rename.
TEST_P(SdsDynamicKeyRotationIntegrationTest, BasicRotation) {
  v3_resource_api_ = true;
  TestEnvironment::exec(
      {TestEnvironment::runfilesPath("test/integration/sds_dynamic_key_rotation_setup.sh")});

  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getCurrentServerSecret());
  };
  initialize();

  // Initial update from filesystem.
  waitForSdsUpdateStats(1);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  // First request with server{cert,key}.pem.
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());
  cleanupUpstreamAndDownstream();
  // Rotate.
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("root/new"),
                              TestEnvironment::temporaryPath("root/current"));
  waitForSdsUpdateStats(2);
  // The rotation is not a SDS attempt, so no change to these stats.
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_rsa.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server_cert_rsa.update_rejected")->value());

  // First request with server_ecdsa{cert,key}.pem.
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());
}

// Validate that rotating to a directory with missing certs is handled.
TEST_P(SdsDynamicKeyRotationIntegrationTest, EmptyRotation) {
  v3_resource_api_ = true;
  TestEnvironment::exec(
      {TestEnvironment::runfilesPath("test/integration/sds_dynamic_key_rotation_setup.sh")});

  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getCurrentServerSecret());
  };
  initialize();

  // Initial update from filesystem.
  waitForSdsUpdateStats(1);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  // First request with server{cert,key}.pem.
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());
  cleanupUpstreamAndDownstream();

  // Rotate to an empty directory, this should fail.
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("root/empty"),
                              TestEnvironment::temporaryPath("root/current"));
  test_server_->waitForCounterEq("sds.server_cert_rsa.key_rotation_failed", 1);
  waitForSdsUpdateStats(1);
  // The rotation is not a SDS attempt, so no change to these stats.
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_rsa.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server_cert_rsa.update_rejected")->value());

  // Requests continue to work with key/cert pair.
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());
}

// A test that SDS server send a good server secret for a static listener.
// The first ssl request should be OK.
TEST_P(SdsDynamicDownstreamIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getServerSecretRsa());
  };
  initialize();

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());

  // Success
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_rsa.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server_cert_rsa.update_rejected")->value());
}

TEST_P(SdsDynamicDownstreamIntegrationTest, DualCert) {
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getServerSecretRsa());
  };

  dual_cert_ = true;
  initialize();

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };

  client_ssl_ctx_ = createClientSslTransportSocketFactory(
      ClientSslTransportOptions()
          .setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2)
          .setCipherSuites({"ECDHE-ECDSA-AES128-GCM-SHA256"}),
      context_manager_, *api_);
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());

  cleanupUpstreamAndDownstream();
  client_ssl_ctx_ = createClientSslTransportSocketFactory(
      ClientSslTransportOptions()
          .setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2)
          .setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"}),
      context_manager_, *api_);
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());

  // Success
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_rsa.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server_cert_rsa.update_rejected")->value());
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_ecdsa.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server_cert_ecdsa.update_rejected")->value());

  // QUIC ignores the `setTlsVersion` set above and always uses TLS 1.3, and TLS 1.3 ignores the
  // `setCipherSuites`, so in the QUIC config, this test only uses one of the certs.
  if (!test_quic_) {
    EXPECT_EQ(1,
              test_server_->counter(listenerStatPrefix("ssl.ciphers.ECDHE-RSA-AES128-GCM-SHA256"))
                  ->value());
    EXPECT_EQ(1,
              test_server_->counter(listenerStatPrefix("ssl.ciphers.ECDHE-ECDSA-AES128-GCM-SHA256"))
                  ->value());
  }
}

// A test that multiple(>2) sds configs are provided. More than one RSA/ECDSA cert
// is allowed if they has different server name pattern(DNS SAN or subject name),
// different server name patterns will be used to match SNI in cert selection.
// Detailed behaviors of cert selection are tested in SslSocketTest with static
// tls_certificates config. This test is to verify multiple certificates can be loaded
// via sds and give a simple case of selecting cert based on SNI.
TEST_P(SdsDynamicDownstreamIntegrationTest, MultipleCerts) {
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getServerSecretRsa());
  };

  multi_cert_ = true;
  initialize();

  client_ssl_ctx_ = createClientSslTransportSocketFactory(
      ClientSslTransportOptions()
          .setSni("www.lyft.com")
          .setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2)
          .setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"}),
      context_manager_, *api_);
  auto ssl_client1 = makeSslClientConnection();
  codec_client_ = makeRawHttpConnection(std::move(ssl_client1), absl::nullopt);
  EXPECT_TRUE(codec_client_->connected());
  codec_client_->connection()->close(Network::ConnectionCloseType::NoFlush);
  // peer certificate is not present when using QUIC
  if (!test_quic_) {
    EXPECT_EQ(TEST_SERVER_CERT_1_HASH,
              codec_client_->connection()->ssl()->sha1PeerCertificateDigest());
    EXPECT_EQ("www.lyft.com", codec_client_->connection()->ssl()->sni());
  }

  cleanupUpstreamAndDownstream();
  client_ssl_ctx_ = createClientSslTransportSocketFactory(
      ClientSslTransportOptions()
          .setSni("www.lyft2.com")
          .setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2)
          .setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"}),
      context_manager_, *api_);
  auto ssl_client2 = makeSslClientConnection();
  codec_client_ = makeRawHttpConnection(std::move(ssl_client2), absl::nullopt);
  EXPECT_TRUE(codec_client_->connected());
  codec_client_->connection()->close(Network::ConnectionCloseType::NoFlush);
  // peer certificate is not present when using QUIC
  if (!test_quic_) {
    EXPECT_EQ(TEST_SERVER2_CERT_1_HASH,
              codec_client_->connection()->ssl()->sha1PeerCertificateDigest());
    EXPECT_EQ("www.lyft2.com", codec_client_->connection()->ssl()->sni());
  }

  // Success
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_rsa.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server_cert_rsa.update_rejected")->value());
  EXPECT_EQ(1, test_server_->counter("sds.server2_cert_rsa.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server2_cert_rsa.update_rejected")->value());
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_ecdsa.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server_cert_ecdsa.update_rejected")->value());
}

// A test that SDS server send a bad secret for a static listener,
// The first ssl request should fail at connecting.
// then SDS send a good server secret,  the second request should be OK.
TEST_P(SdsDynamicDownstreamIntegrationTest, WrongSecretFirst) {
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getWrongSecret(server_cert_rsa_));
  };
  initialize();

  codec_client_ = makeRawHttpConnection(makeSslClientConnection(), absl::nullopt);
  // the connection state is not connected.
  EXPECT_FALSE(codec_client_->connected());
  codec_client_->connection()->close(Network::ConnectionCloseType::NoFlush);

  // Failure
  EXPECT_EQ(0, test_server_->counter("sds.server_cert_rsa.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_rsa.update_rejected")->value());

  sendSdsResponse(getServerSecretRsa());

  // Wait for sds update counter.
  waitForSdsUpdateStats(1);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());

  // Success
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_rsa.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_rsa.update_rejected")->value());
}

class SdsDynamicDownstreamCertValidationContextTest : public SdsDynamicDownstreamIntegrationTest {
public:
  SdsDynamicDownstreamCertValidationContextTest() = default;

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      config_helper_.configDownstreamTransportSocketWithTls(
          bootstrap,
          [this](
              envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
            common_tls_context.add_alpn_protocols(test_quic_
                                                      ? Http::Utility::AlpnNames::get().Http3
                                                      : Http::Utility::AlpnNames::get().Http11);
            configureInlinedCerts(&common_tls_context);
          });

      // Add a static SDS cluster as the first cluster in the list.
      // The SDS cluster needs to appear before the cluster that uses it for secrets, so that it
      // gets initialized first.
      bootstrap.mutable_static_resources()->add_clusters()->MergeFrom(
          bootstrap.static_resources().clusters(0));
      auto* sds_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      sds_cluster->set_name("sds_cluster.lyft.com");
      sds_cluster->mutable_load_assignment()->set_cluster_name("sds_cluster.lyft.com");
      ConfigHelper::setHttp2(*sds_cluster);

      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext upstream_tls_context;
      if (share_validation_secret_) {
        // Configure static cluster with SDS config referencing "validation_secret",
        // which is going to be processed before LDS resources.
        ASSERT(use_lds_);
        setUpSdsValidationContext(upstream_tls_context.mutable_common_tls_context());
      }
      // Enable SSL/TLS with a client certificate in the first cluster.
      auto* upstream_tls_certificate =
          upstream_tls_context.mutable_common_tls_context()->add_tls_certificates();
      upstream_tls_certificate->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
      upstream_tls_certificate->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
      auto* upstream_transport_socket = bootstrap.mutable_static_resources()
                                            ->mutable_clusters(dataPlaneUpstreamIndex())
                                            ->mutable_transport_socket();
      upstream_transport_socket->set_name("envoy.transport_sockets.tls");
      upstream_transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
    });

    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
    client_ssl_ctx_ = createClientSslTransportSocketFactory({}, context_manager_, *api_);
  }

  void configureInlinedCerts(
      envoy::extensions::transport_sockets::tls::v3::CommonTlsContext* common_tls_context) {
    auto* tls_certificate = common_tls_context->add_tls_certificates();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
    setUpSdsValidationContext(common_tls_context);
  }

  void setUpSdsValidationContext(
      envoy::extensions::transport_sockets::tls::v3::CommonTlsContext* common_tls_context) {
    if (use_combined_validation_context_) {
      // Modify the listener context validation type to use combined certificate validation
      // context.
      auto* combined_config = common_tls_context->mutable_combined_validation_context();
      auto* default_validation_context = combined_config->mutable_default_validation_context();
      default_validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);
      auto* secret_config = combined_config->mutable_validation_context_sds_secret_config();
      setUpSdsConfig(secret_config, validation_secret_);
    } else {
      // Modify the listener context validation type to use dynamic certificate validation
      // context.
      auto* secret_config = common_tls_context->mutable_validation_context_sds_secret_config();
      setUpSdsConfig(secret_config, validation_secret_);
    }
  }

  void createUpstreams() override {
    // SDS cluster for the first cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
    // Fake upstream with SSL/TLS for the second cluster.
    addFakeUpstream(createUpstreamSslContext(), upstreamProtocol(), /*autonomous_upstream=*/false);
    xds_upstream_ = fake_upstreams_.front().get();
  }

  Network::DownstreamTransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    auto* tls_certificate = common_tls_context->add_tls_certificates();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));

    auto cfg = *Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(
        tls_context, factory_context_, false);
    static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
    return Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
               std::move(cfg), context_manager_, *upstream_stats_store->rootScope(),
               std::vector<std::string>{})
        .value();
  }

  void TearDown() override {
    cleanUpXdsConnection();

    client_ssl_ctx_.reset();
    cleanupUpstreamAndDownstream();
    codec_client_.reset();
  }

  void enableCombinedValidationContext(bool enable) { use_combined_validation_context_ = enable; }
  void shareValidationSecret(bool share) { share_validation_secret_ = share; }

private:
  bool use_combined_validation_context_{false};
  bool share_validation_secret_{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, SdsDynamicDownstreamCertValidationContextTest,
                         testing::ValuesIn(getSdsTestsParams()), sdsTestParamsToString);

// A test that SDS server send a good certificate validation context for a static listener.
// The first ssl request should be OK.
TEST_P(SdsDynamicDownstreamCertValidationContextTest, BasicSuccess) {
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getCvcSecret());
  };
  initialize();

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());

  // Success
  EXPECT_EQ(1, test_server_->counter("sds.validation_secret.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.validation_secret.update_rejected")->value());
}

// A test that SDS server sends a certificate validation context for a static listener.
// Listener combines default certificate validation context and the dynamic one.
// The first ssl request should be OK.
TEST_P(SdsDynamicDownstreamCertValidationContextTest, CombinedCertValidationContextSuccess) {
  enableCombinedValidationContext(true);
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getCvcSecretWithOnlyTrustedCa());
  };
  initialize();

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());

  // Success
  EXPECT_EQ(1, test_server_->counter("sds.validation_secret.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.validation_secret.update_rejected")->value());
}

// A test that verifies that both: static cluster and LDS listener are updated when using
// the same verification secret (standalone validation context) from the SDS server.
TEST_P(SdsDynamicDownstreamCertValidationContextTest, BasicWithSharedSecret) {
  shareValidationSecret(true);
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getCvcSecret());
  };
  initialize();

  // Wait for "ssl_context_updated_by_sds" counters to indicate that both resources
  // depending on the verification_secret were updated.
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);
  waitForSdsUpdateStats(1);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());

  // Success
  EXPECT_EQ(1, test_server_->counter("sds.validation_secret.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.validation_secret.update_rejected")->value());
}

// A test that verifies that both: static cluster and LDS listener are updated when using
// the same verification secret (combined validation context) from the SDS server.
TEST_P(SdsDynamicDownstreamCertValidationContextTest, CombinedValidationContextWithSharedSecret) {
  enableCombinedValidationContext(true);
  shareValidationSecret(true);
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getCvcSecretWithOnlyTrustedCa());
  };
  initialize();

  // Wait for "ssl_context_updated_by_sds" counters to indicate that both resources
  // depending on the verification_secret were updated.
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);
  waitForSdsUpdateStats(1);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());

  // Success
  EXPECT_EQ(1, test_server_->counter("sds.validation_secret.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.validation_secret.update_rejected")->value());
}

// Upstream SDS integration test: a static cluster has ssl cert from SDS.
class SdsDynamicUpstreamIntegrationTest : public SdsDynamicIntegrationBaseTest {
public:
  void initialize() override {
    // The SNI of the certificates loaded in this test.
    default_request_headers_.setHost("www.lyft.com");
    if (test_quic_) {
      upstream_tls_ = true;
      setUpstreamProtocol(Http::CodecType::HTTP3);
    }
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add a static SDS cluster as the first cluster in the list.
      // The SDS cluster needs to appear before the cluster that uses it for secrets, so that it
      // gets initialized first.
      bootstrap.mutable_static_resources()->add_clusters()->MergeFrom(
          bootstrap.static_resources().clusters(0));
      auto* sds_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      sds_cluster->set_name("sds_cluster.lyft.com");
      sds_cluster->mutable_load_assignment()->set_cluster_name("sds_cluster.lyft.com");
      ConfigHelper::setHttp2(*sds_cluster);

      // Unwind Quic for SDS cluster.
      if (test_quic_) {
        sds_cluster->clear_transport_socket();
      }

      // change the first cluster with ssl and sds.
      auto* transport_socket = bootstrap.mutable_static_resources()
                                   ->mutable_clusters(dataPlaneUpstreamIndex())
                                   ->mutable_transport_socket();
      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
      tls_context.set_sni("lyft.com");
      auto* secret_config =
          tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
      setUpSdsConfig(secret_config, "client_cert");

      if (test_quic_) {
        envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport quic_context;
        quic_context.mutable_upstream_tls_context()->CopyFrom(tls_context);
        transport_socket->set_name("envoy.transport_sockets.quic");
        transport_socket->mutable_typed_config()->PackFrom(quic_context);
      } else {
        transport_socket->set_name("envoy.transport_sockets.tls");
        transport_socket->mutable_typed_config()->PackFrom(tls_context);
      }
    });

    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
  }

  void TearDown() override {
    cleanUpXdsConnection();

    cleanupUpstreamAndDownstream();
    codec_client_.reset();

    test_server_.reset();
    fake_upstreams_.clear();
  }

  void createUpstreams() override {
    // SDS cluster for the first cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
    // FakeUpstream with SSL/TLS for the second cluster.
    addFakeUpstream(createUpstreamSslContext(context_manager_, *api_, test_quic_),
                    upstreamProtocol(), /*autonomous_upstream=*/false);
    xds_upstream_ = fake_upstreams_.front().get();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SdsDynamicUpstreamIntegrationTest,
                         testing::ValuesIn(getSdsTestsParams()), sdsTestParamsToString);

// To test a static cluster with sds. SDS send a good client secret first.
// The first request should work.
TEST_P(SdsDynamicUpstreamIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getClientSecret());
  };

  initialize();

  // There is a race condition here; there are two static clusters:
  // backend cluster_0 with sds and sds_cluster. cluster_0 is created first, its init_manager
  // is called so it issues a sds call, but fail since sds_cluster is not added yet.
  // so cluster_0 is initialized with an empty secret. initialize() will not wait and will return.
  // the testing request will be called, even though in the pre_worker_function, a good sds is
  // send, the cluster will be updated with good secret, the testing request may fail if it is
  // before context is updated. Hence, need to wait for context_update counter.
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);

  testRouterHeaderOnlyRequestAndResponse(/*create_connection=*/nullptr, dataPlaneUpstreamIndex());

  // Success
  EXPECT_EQ(1, test_server_->counter("sds.client_cert.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.client_cert.update_rejected")->value());
}

// Tests a static cluster with SDS.
// SDS sends a bad client secret first, so the request fails with a 503.
// Then, SDS sends a good client secret, so the second request is successful.
TEST_P(SdsDynamicUpstreamIntegrationTest, WrongSecretFirst) {
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getWrongSecret(client_cert_));
  };
  initialize();

  // Make a simple request, should get 503
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/test/long/url", "", downstream_protocol_, version_);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  // Wait for the raw TCP connection with bad credentials and close it.
  if (upstreamProtocol() != Http::CodecType::HTTP3) {
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(
        fake_upstreams_[dataPlaneUpstreamIndex()]->waitForRawConnection(fake_upstream_connection));
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  }

  test_server_->waitForCounterGe("sds.client_cert.update_rejected", 1);
  EXPECT_EQ(0, test_server_->counter("sds.client_cert.update_success")->value());

  sendSdsResponse(getClientSecret());
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);

  test_server_->waitForCounterGe("sds.client_cert.update_success", 1);
  EXPECT_EQ(1, test_server_->counter("sds.client_cert.update_rejected")->value());

  // Verify the update succeeded.
  testRouterHeaderOnlyRequestAndResponse(/*create_connection=*/nullptr, dataPlaneUpstreamIndex());
}

// Test CDS with SDS. A cluster provided by CDS raises new SDS request for upstream cert.
// TODO(15034) Enable SDS support in QUIC upstream.
class SdsCdsIntegrationTest : public SdsDynamicIntegrationBaseTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // The 1st cluster, which already exists in the Bootstrap config, will be the CDS cluster.
      // The 2nd cluster, which will be added, will be the SDS cluster.
      // The 3rd cluster, which will be added after that, will be the dynamic cluster, which will
      // use SDS.
      // The xDS clusters need to appear before the cluster that uses it, so that they get
      // initialized first.

      // The SDS cluster.
      auto* sds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      sds_cluster->MergeFrom(bootstrap.static_resources().clusters(0));
      // The static cluster.
      auto* static_cluster = bootstrap.mutable_static_resources()->add_clusters();
      static_cluster->MergeFrom(bootstrap.static_resources().clusters(0));
      // Make a copy of the static cluster to create and set up the dynamic cluster.
      dynamic_cluster_ = *static_cluster;

      // Set up the first cluster, the CDS cluster.
      auto* cds_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cds_cluster->set_name("cds_cluster");
      cds_cluster->mutable_load_assignment()->set_cluster_name("cds_cluster");
      ConfigHelper::setHttp2(*cds_cluster);

      // Set up the second cluster, the SDS cluster.
      sds_cluster->set_name("sds_cluster.lyft.com");
      sds_cluster->mutable_load_assignment()->set_cluster_name("sds_cluster.lyft.com");
      ConfigHelper::setHttp2(*sds_cluster);

      // Set up the dynamic cluster to use SDS.
      dynamic_cluster_.set_name("dynamic");
      dynamic_cluster_.mutable_connect_timeout()->MergeFrom(
          ProtobufUtil::TimeUtil::MillisecondsToDuration(500000));
      auto* transport_socket = dynamic_cluster_.mutable_transport_socket();
      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
      auto* secret_config =
          tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
      setUpSdsConfig(secret_config, "client_cert");

      transport_socket->set_name("envoy.transport_sockets.tls");
      transport_socket->mutable_typed_config()->PackFrom(tls_context);

      // Set up the Bootstrap's CDS config to fetch from the CDS cluster.
      const std::string cds_yaml = R"EOF(
    api_config_source:
      api_type: GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: cds_cluster
      set_node_on_first_message_only: true
)EOF";
      auto* cds = bootstrap.mutable_dynamic_resources()->mutable_cds_config();
      TestUtility::loadFromYaml(cds_yaml, *cds);
    });

    HttpIntegrationTest::initialize();
  }

  void TearDown() override {
    {
      AssertionResult result = sds_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = sds_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      sds_connection_.reset();
    }
    cleanUpXdsConnection();
    cleanupUpstreamAndDownstream();
    codec_client_.reset();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void createUpstreams() override {
    // CDS Cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
    // SDS Cluster.
    addFakeUpstream(Http::CodecType::HTTP2);
    // Static cluster.
    addFakeUpstream(Http::CodecType::HTTP1);
    xds_upstream_ = fake_upstreams_.front().get();
  }

  std::unique_ptr<FakeUpstream>& cdsUpstream() { return fake_upstreams_[0]; }
  std::unique_ptr<FakeUpstream>& sdsUpstream() override { return fake_upstreams_[1]; }

  void sendCdsResponse() {
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {dynamic_cluster_}, {dynamic_cluster_}, {}, "55");
  }

  void sendSdsResponse2(const envoy::extensions::transport_sockets::tls::v3::Secret& secret,
                        FakeStream& sds_stream) {
    envoy::service::discovery::v3::DiscoveryResponse discovery_response;
    discovery_response.set_version_info("1");
    discovery_response.set_type_url(Config::TypeUrl::get().Secret);
    discovery_response.add_resources()->PackFrom(secret);
    sds_stream.sendGrpcMessage(discovery_response);
  }

  envoy::config::cluster::v3::Cluster dynamic_cluster_;
  FakeHttpConnectionPtr sds_connection_;
  FakeStreamPtr sds_stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SdsCdsIntegrationTest, testing::ValuesIn(getSdsTestsParams()),
                         sdsTestParamsToString);

TEST_P(SdsCdsIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [this]() {
    {
      // CDS.
      AssertionResult result = cdsUpstream()->waitForHttpConnection(*dispatcher_, xds_connection_);
      RELEASE_ASSERT(result, result.message());
      result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      xds_stream_->startGrpcStream();
      sendCdsResponse();
    }
    {
      // SDS.
      AssertionResult result = sdsUpstream()->waitForHttpConnection(*dispatcher_, sds_connection_);
      RELEASE_ASSERT(result, result.message());

      result = sds_connection_->waitForNewStream(*dispatcher_, sds_stream_);
      RELEASE_ASSERT(result, result.message());
      sds_stream_->startGrpcStream();
      sendSdsResponse2(getClientSecret(), *sds_stream_);
    }
  };
  initialize();

  test_server_->waitForCounterGe(
      "cluster.dynamic.client_ssl_socket_factory.ssl_context_update_by_sds", 1);
  // The 4 clusters are CDS,SDS,static and dynamic cluster.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 4);

  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster, {}, {},
                                                             {}, "42");
  // Successfully removed the dynamic cluster.
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);
}

class SdsDynamicDownstreamPrivateKeyIntegrationTest : public SdsDynamicDownstreamIntegrationTest {
public:
  envoy::extensions::transport_sockets::tls::v3::Secret getCurrentServerPrivateKeyProviderSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;

    const std::string yaml =
        R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_tmpdir }}/root/current/servercert.pem"
  private_key_provider:
    provider_name: test
    typed_config:
      "@type": "type.googleapis.com/google.protobuf.Struct"
      value:
        private_key_file: "{{ test_tmpdir }}/root/current/serverkey.pem"
        expected_operation: "sign"
        sync_mode: true
        mode: "rsa"
)EOF";

    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret);
    secret.set_name(server_cert_rsa_);

    return secret;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, SdsDynamicDownstreamPrivateKeyIntegrationTest,
                         testing::ValuesIn(getSdsTestsParams(true)), sdsTestParamsToString);

// Validate that a basic SDS updates work with a private key provider.
TEST_P(SdsDynamicDownstreamPrivateKeyIntegrationTest, BasicPrivateKeyProvider) {
  v3_resource_api_ = true;

  TestEnvironment::exec(
      {TestEnvironment::runfilesPath("test/integration/sds_dynamic_key_rotation_setup.sh")});

  // Set up the private key provider.
  Extensions::PrivateKeyMethodProvider::TestPrivateKeyMethodFactory test_factory;
  Registry::InjectFactory<Ssl::PrivateKeyMethodProviderInstanceFactory>
      test_private_key_method_factory(test_factory);

  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getCurrentServerPrivateKeyProviderSecret());
  };
  initialize();

  EXPECT_EQ(1, test_server_->counter("sds.server_cert_rsa.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server_cert_rsa.update_rejected")->value());

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());

  cleanupUpstreamAndDownstream();
}

class SdsCdsPrivateKeyIntegrationTest : public SdsCdsIntegrationTest {
public:
  envoy::extensions::transport_sockets::tls::v3::Secret getCurrentServerPrivateKeyProviderSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    const std::string yaml =
        R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_tmpdir }}/root/current/servercert.pem"
  private_key_provider:
    provider_name: test
    typed_config:
      "@type": "type.googleapis.com/google.protobuf.Struct"
      value:
        private_key_file: "{{ test_tmpdir }}/root/current/serverkey.pem"
        expected_operation: "sign"
        sync_mode: true
        mode: "rsa"
)EOF";

    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret);
    secret.set_name(client_cert_);

    return secret;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, SdsCdsPrivateKeyIntegrationTest,
                         testing::ValuesIn(getSdsTestsParams(true)), sdsTestParamsToString);

// Test private key providers in SDS+CDS setup.
TEST_P(SdsCdsPrivateKeyIntegrationTest, BasicSdsCdsPrivateKeyProvider) {
  v3_resource_api_ = true;

  TestEnvironment::exec(
      {TestEnvironment::runfilesPath("test/integration/sds_dynamic_key_rotation_setup.sh")});

  // Set up the private key provider.
  Extensions::PrivateKeyMethodProvider::TestPrivateKeyMethodFactory test_factory;
  Registry::InjectFactory<Ssl::PrivateKeyMethodProviderInstanceFactory>
      test_private_key_method_factory(test_factory);

  on_server_init_function_ = [this]() {
    {
      // CDS.
      AssertionResult result = cdsUpstream()->waitForHttpConnection(*dispatcher_, xds_connection_);
      EXPECT_TRUE(result);
      result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      EXPECT_TRUE(result);
      xds_stream_->startGrpcStream();
      sendCdsResponse();
    }
    {
      // SDS.
      AssertionResult result = sdsUpstream()->waitForHttpConnection(*dispatcher_, sds_connection_);
      EXPECT_TRUE(result);

      result = sds_connection_->waitForNewStream(*dispatcher_, sds_stream_);
      EXPECT_TRUE(result);
      sds_stream_->startGrpcStream();
      sendSdsResponse2(getCurrentServerPrivateKeyProviderSecret(), *sds_stream_);
    }
  };
  initialize();

  test_server_->waitForCounterGe(
      "cluster.dynamic.client_ssl_socket_factory.ssl_context_update_by_sds", 1);
  // The 4 clusters are CDS,SDS,static and dynamic cluster.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 4);

  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster, {}, {},
                                                             {}, "42");
  // Successfully removed the dynamic cluster.
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);
}

} // namespace Ssl
} // namespace Envoy
