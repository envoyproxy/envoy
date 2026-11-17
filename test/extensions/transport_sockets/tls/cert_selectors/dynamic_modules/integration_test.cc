#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "envoy/admin/v3/certs.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"

#include "source/common/tls/ssl_handshaker.h"

#include "test/common/tls/integration/ssl_integration_test_base.h"
#include "test/config/integration/certs/client_ecdsacert_hash.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/config/integration/certs/concatenated_dns_servercert_hash.h"
#include "test/config/integration/certs/server2cert_hash.h"
#include "test/config/integration/certs/server_ecdsacert_hash.h"
#include "test/config/integration/certs/servercert_hash.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {
namespace {

// Returns the certificate selector config for a selector of the test module. The "downstream" and
// "upstream" selectors select the last certificate for the server name in the config and the first
// one otherwise, the "filter_state" selector selects the second certificate when the filter state
// "tenant" is the config and the first one otherwise, and the "fail" selector fails every
// selection. The "dynamic" selector uses the dynamic certificate named by the server name and lets
// Envoy select the certificate for a server name with the "www." prefix. Its config lines provide
// the "prefetch" certificates when the config is created, the "sync" certificates when they are
// requested, the "fetch" certificates from another thread when they are requested, the "hold"
// certificates only when a name with the "release." prefix is requested, and the "init"
// certificates from another thread before it signals that its initialization is complete. It
// declines other names. The extra fields are added to the DynamicModuleTlsCertificateSelector.
std::string selectorConfig(absl::string_view selector_name, absl::string_view config,
                           absl::string_view extra_fields = "") {
  // The module runs its own threads, so it stays loaded.
  return fmt::format(R"EOF(
name: envoy.tls.certificate_selectors.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_selectors.dynamic_modules.v3.DynamicModuleTlsCertificateSelector
  dynamic_module_config:
    name: tls_certificate_selector_integration_test
    do_not_close: true
  selector_name: {}
  selector_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: "{}"
  allow_dynamic_certificates: {}
  {}
)EOF",
                     selector_name, config, selector_name == "dynamic", extra_fields);
}

std::string dynamicSelectorConfig(const std::vector<std::string>& certificates,
                                  absl::string_view extra_fields = "") {
  return selectorConfig("dynamic", absl::StrJoin(certificates, "\\n"), extra_fields);
}

// Returns a config line of the "dynamic" selector for a certificate of the test certificates, with
// its OCSP response when ocsp_staple is set.
std::string dynamicCertificate(absl::string_view mode, absl::string_view name,
                               absl::string_view certificate, bool ocsp_staple = false) {
  const std::string path =
      TestEnvironment::runfilesPath(absl::StrCat("test/config/integration/certs/", certificate));
  std::string line = absl::StrCat(mode, " ", name, " ", path, "cert.pem ", path, "key.pem");
  if (ocsp_staple) {
    absl::StrAppend(&line, " ", path, "_ocsp_resp.der");
  }
  return line;
}

// The certificate hash headers are uppercase and colon separated, while the certificate digests of
// a connection are lowercase hex.
std::string certificateDigest(absl::string_view hash) {
  return absl::AsciiStrToLower(absl::StrReplaceAll(hash, {{":", ""}}));
}

void setModulesSearchPath() {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
      1);
}

// Returns the BoringSSL connection of a TLS client connection.
SSL* clientSsl(const Network::ClientConnection& connection) {
  const auto* handshaker =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          connection.ssl().get());
  RELEASE_ASSERT(handshaker != nullptr, "the client connection must use TLS");
  return handshaker->ssl();
}

// Expects the client to offer a session that the server could resume.
void expectResumableSession(const SSL_SESSION* session) {
  ASSERT_NE(nullptr, session);
  EXPECT_TRUE(SSL_SESSION_is_resumable(session));
}

class DynamicModuleTlsCertificateSelectorIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public SslIntegrationTestBase {
public:
  DynamicModuleTlsCertificateSelectorIntegrationTest() : SslIntegrationTestBase(GetParam()) {}
  void TearDown() override { SslIntegrationTestBase::TearDown(); }

  void initialize() override {
    setModulesSearchPath();
    // The listener has an RSA certificate followed by an ECDSA certificate.
    server_ecdsa_cert_ = true;
    SslIntegrationTestBase::initialize();
  }

  // Initializes the listener with the "dynamic" selector and the extra fields.
  void initializeWithDynamicCertificates(const std::vector<std::string>& certificates,
                                         absl::string_view extra_fields = "") {
    tls_cert_selector_yaml_ = dynamicSelectorConfig(certificates, extra_fields);
    initialize();
  }

  // Sends a request with the given server name and expects the certificate with the given hash.
  void expectServedCertificate(absl::string_view server_name, absl::string_view certificate_hash,
                               ClientSslTransportOptions options = ClientSslTransportOptions()) {
    expectServedCertificate(makeSslClientConnection(options.setSni(server_name)), certificate_hash);
  }

  // Sends a request over the connection and expects the certificate with the given hash. The
  // response arrives after the TLS 1.3 session tickets, so the client has cached them.
  void expectServedCertificate(Network::ClientConnectionPtr&& connection,
                               absl::string_view certificate_hash) {
    codec_client_ = makeHttpConnection(std::move(connection));
    auto response =
        sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(certificateDigest(certificate_hash),
              codec_client_->connection()->ssl()->sha256PeerCertificateDigest());
    codec_client_->close();
  }

  // Expects the handshake of a client with the given options to fail.
  void expectFailedHandshake(const ClientSslTransportOptions& options) {
    expectFailedHandshake(makeSslClientConnection(options));
  }

  // Expects the handshake of the connection to fail.
  void expectFailedHandshake(Network::ClientConnectionPtr&& connection) {
    auto codec_client = makeRawHttpConnection(std::move(connection), std::nullopt);
    EXPECT_FALSE(codec_client->connected());
  }

  // Creates a client transport socket factory, whose connections share its session cache.
  Network::UpstreamTransportSocketFactoryPtr
  createClientFactory(const ClientSslTransportOptions& options) {
    return createClientSslTransportSocketFactory(options, *context_manager_, *api_,
                                                 &server_factory_context_.serverScope());
  }

  // Creates a client connection with a transport socket of the factory.
  Network::ClientConnectionPtr makeSslClientConnectionFromFactory(
      const Network::UpstreamTransportSocketFactory& client_factory) {
    return dispatcher_->createClientConnection(
        getSslAddress(version_, lookupPort("http")), Network::Address::InstanceConstSharedPtr(),
        client_factory.createTransportSocket({}, nullptr), nullptr, nullptr);
  }

  // Expects the second of two connections of the client factory to resume the session of the first.
  void expectResumedSession(const Network::UpstreamTransportSocketFactory& client_factory,
                            absl::string_view certificate_hash) {
    expectServedCertificate(makeSslClientConnectionFromFactory(client_factory), certificate_hash);
    expectServedCertificate(makeSslClientConnectionFromFactory(client_factory), certificate_hash);
    test_server_->waitForCounter(listenerStatPrefix("ssl.session_reused"), testing::Eq(1));
  }

  // Offers the session of the dynamic certificate for "first.lyft.com" in a handshake for
  // "second.lyft.com", whose certificate has names that concatenate to the same bytes, and expects
  // the handshake to resume the session only when expect_resumption is set.
  void offerSessionForConcatenatedNames(bool expect_resumption) {
    initializeWithDynamicCertificates(
        {dynamicCertificate("prefetch", "first.lyft.com", "server"),
         dynamicCertificate("prefetch", "second.lyft.com", "concatenated_dns_server")});
    ClientSslTransportOptions options;
    options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
    expectServedCertificate(
        makeSslClientConnection(ClientSslTransportOptions(options).setSni("first.lyft.com")),
        TEST_SERVER_CERT_HASH);
    bssl::UniquePtr<SSL_SESSION> session(SSL_get1_session(clientSsl(*codec_client_->connection())));
    ASSERT_NO_FATAL_FAILURE(expectResumableSession(session.get()));

    Network::ClientConnectionPtr connection =
        makeSslClientConnection(ClientSslTransportOptions(options).setSni("second.lyft.com"));
    ASSERT_EQ(1, SSL_set_session(clientSsl(*connection), session.get()));
    // A resumed session keeps the certificate of the handshake that established it.
    expectServedCertificate(std::move(connection), expect_resumption
                                                       ? TEST_SERVER_CERT_HASH
                                                       : TEST_CONCATENATED_DNS_SERVER_CERT_HASH);
    test_server_->waitForCounter(listenerStatPrefix("ssl.handshake"), testing::Eq(2));
    EXPECT_EQ(expect_resumption ? 1U : 0U,
              test_server_->counter(listenerStatPrefix("ssl.session_reused"))->value());
  }

  void waitForModuleCounter(absl::string_view name, uint64_t value,
                            Event::Dispatcher* dispatcher = nullptr) {
    test_server_->waitForCounter(absl::StrCat("dynamicmodulescustom.", name), testing::Eq(value),
                                 TestUtility::DefaultTimeout, dispatcher);
  }

  // Starts a handshake with the given server name without waiting for it.
  Network::ClientConnectionPtr startHandshake(absl::string_view server_name) {
    Network::ClientConnectionPtr connection =
        makeSslClientConnection(ClientSslTransportOptions().setSni(server_name));
    connection->connect();
    return connection;
  }

  // Flushes the stats every 100 milliseconds, since a flush updates the server statistics of the
  // certificates.
  void flushStatsOften() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_stats_flush_interval()->set_nanos(100 * 1000 * 1000);
    });
  }

  // Waits for a stats flush that sets the expiration of the first OCSP response to a matching
  // value.
  void waitForOcspResponseExpiration(testing::Matcher<uint64_t> matcher) {
    test_server_->waitForGauge("server.seconds_until_first_ocsp_response_expiring", matcher);
  }

  // Waits for a stats flush that sets the days until the first certificate expires to a matching
  // value.
  void waitForCertificateExpiration(testing::Matcher<uint64_t> matcher) {
    test_server_->waitForGauge("server.days_until_first_cert_expiring", matcher);
  }

  // Applies the modifier to the TLS context of the listener.
  void modifyListenerTlsContext(
      std::function<void(envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext&)>&&
          modifier) {
    config_helper_.addConfigModifier(
        [modifier = std::move(modifier)](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* transport_socket = bootstrap.mutable_static_resources()
                                       ->mutable_listeners(0)
                                       ->mutable_filter_chains(0)
                                       ->mutable_transport_socket();
          envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
          ASSERT_TRUE(transport_socket->typed_config().UnpackTo(&tls_context));
          modifier(tls_context);
          ASSERT_TRUE(transport_socket->mutable_typed_config()->PackFrom(tls_context));
        });
  }

  // Serves the validation context of the listener from a file over SDS.
  void useValidationContextFromSds() {
    modifyListenerTlsContext(
        [this](envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& tls_context) {
          auto* common_tls_context = tls_context.mutable_common_tls_context();
          common_tls_context->clear_validation_context();
          auto* secret_config = common_tls_context->mutable_validation_context_sds_secret_config();
          secret_config->set_name("validation");
          auto* config_source = secret_config->mutable_sds_config();
          config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
          config_source->mutable_path_config_source()->set_path(validationContextPath());
        });
  }

  // Writes the SDS validation context, which accepts only the client certificate with the given
  // hash. The rename makes the update atomic.
  void writeValidationContext(absl::string_view version, absl::string_view client_hash) {
    const std::string secret = fmt::format(
        R"EOF(
version_info: "{}"
resources:
- "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
  name: validation
  validation_context:
    trusted_ca:
      filename: "{}"
    verify_certificate_hash: "{}"
)EOF",
        version, TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"),
        client_hash);
    TestEnvironment::renameFile(
        TestEnvironment::writeStringToFileForTest("validation.sds.yaml.tmp", secret),
        validationContextPath());
  }

  std::string validationContextPath() {
    return TestEnvironment::temporaryPath("validation.sds.yaml");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleTlsCertificateSelectorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, SelectsCertificateForServerName) {
  tls_cert_selector_yaml_ = selectorConfig("downstream", "www.lyft.com");
  initialize();
  expectServedCertificate("www.lyft.com", TEST_SERVER_ECDSA_CERT_HASH);
}

// The default selection serves the ECDSA certificate to a client that supports it, so serving the
// RSA certificate shows that the module selected it.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, SelectsCertificateForOtherServerName) {
  tls_cert_selector_yaml_ = selectorConfig("downstream", "www.lyft.com");
  initialize();
  expectServedCertificate("lyft.com", TEST_SERVER_CERT_HASH);
}

TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, FailedSelectionFailsHandshake) {
  tls_cert_selector_yaml_ = selectorConfig("fail", "www.lyft.com");
  initialize();
  expectFailedHandshake(ClientSslTransportOptions().setSni("www.lyft.com"));
  test_server_->waitForCounter(listenerStatPrefix("ssl.connection_error"), testing::Ge(1));
}

TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, PassThroughLetsEnvoySelect) {
  initializeWithDynamicCertificates({});
  expectServedCertificate("www.lyft.com", TEST_SERVER_ECDSA_CERT_HASH);
  waitForModuleCounter("selected.outcome.passthrough", 1);
}

TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, ServesPrefetchedDynamicCertificate) {
  initializeWithDynamicCertificates(
      {dynamicCertificate("prefetch", "prefetch.lyft.com", "server2")});
  expectServedCertificate("prefetch.lyft.com", TEST_SERVER2_CERT_HASH);
  waitForModuleCounter("selected.outcome.dynamic", 1);
  EXPECT_EQ(0U, test_server_->counter("dynamicmodulescustom.requested")->value());
}

// The handshake waits until the module provides the certificate while it is requested.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, ServesDynamicCertificateWhenRequested) {
  initializeWithDynamicCertificates({dynamicCertificate("sync", "sync.lyft.com", "server2")});
  expectServedCertificate("sync.lyft.com", TEST_SERVER2_CERT_HASH);
  waitForModuleCounter("requested", 1);
}

// The handshake waits while the module fetches the certificate on another thread, and later
// handshakes find it without asking the module again.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, ServesFetchedDynamicCertificate) {
  initializeWithDynamicCertificates({dynamicCertificate("fetch", "fetch.lyft.com", "server2")});
  expectServedCertificate("fetch.lyft.com", TEST_SERVER2_CERT_HASH);
  expectServedCertificate("fetch.lyft.com", TEST_SERVER2_CERT_HASH);
  waitForModuleCounter("requested", 1);
  waitForModuleCounter("selected.outcome.dynamic", 2);
}

// A TLS 1.3 session resumes from its ticket when the module selects the certificate that
// established it.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, ResumesTicketOfDynamicCertificate) {
  server_tlsv1_3_ = true;
  initializeWithDynamicCertificates(
      {dynamicCertificate("prefetch", "prefetch.lyft.com", "server2")});
  Network::UpstreamTransportSocketFactoryPtr client_factory = createClientFactory(
      ClientSslTransportOptions()
          .setSni("prefetch.lyft.com")
          .setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3));
  expectResumedSession(*client_factory, TEST_SERVER2_CERT_HASH);
  waitForModuleCounter("selected.outcome.dynamic", 2);
}

// With stateless resumption disabled, a TLS 1.2 session resumes from the session cache.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest,
       ResumesCachedSessionOfDynamicCertificate) {
  modifyListenerTlsContext(
      [](envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& tls_context) {
        tls_context.set_disable_stateless_session_resumption(true);
      });
  initializeWithDynamicCertificates(
      {dynamicCertificate("prefetch", "prefetch.lyft.com", "server2")});
  Network::UpstreamTransportSocketFactoryPtr client_factory = createClientFactory(
      ClientSslTransportOptions()
          .setSni("prefetch.lyft.com")
          .setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2));
  expectResumedSession(*client_factory, TEST_SERVER2_CERT_HASH);
  waitForModuleCounter("selected.outcome.dynamic", 2);
}

// A session does not resume once the module replaces its certificate with one for other names.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest,
       DoesNotResumeSessionOfReplacedCertificate) {
  server_tlsv1_3_ = true;
  initializeWithDynamicCertificates({dynamicCertificate("prefetch", "replaced.lyft.com", "server2"),
                                     dynamicCertificate("hold", "replaced.lyft.com", "server")});
  Network::UpstreamTransportSocketFactoryPtr client_factory = createClientFactory(
      ClientSslTransportOptions()
          .setSni("replaced.lyft.com")
          .setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3));
  expectServedCertificate(makeSslClientConnectionFromFactory(*client_factory),
                          TEST_SERVER2_CERT_HASH);

  // The module provides the held certificate when the release name is requested.
  expectFailedHandshake(ClientSslTransportOptions().setSni("release.lyft.com"));
  Network::ClientConnectionPtr connection = makeSslClientConnectionFromFactory(*client_factory);
  // The client offers the ticket of the first session.
  ASSERT_NO_FATAL_FAILURE(expectResumableSession(SSL_get_session(clientSsl(*connection))));
  expectServedCertificate(std::move(connection), TEST_SERVER_CERT_HASH);
  test_server_->waitForCounter(listenerStatPrefix("ssl.handshake"), testing::Eq(2));
  EXPECT_EQ(0U, test_server_->counter(listenerStatPrefix("ssl.session_reused"))->value());
}

// A session does not resume on a dynamic certificate for other names, even when the names of the
// two certificates concatenate to the same bytes.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest,
       DoesNotResumeSessionOnCertificateWithConcatenatedNames) {
  offerSessionForConcatenatedNames(/*expect_resumption=*/false);
}

// Without the runtime guard, the session context IDs of the two certificates hash the same bytes,
// so the session resumes.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest,
       ResumesSessionOnCertificateWithConcatenatedNamesWithoutRuntimeGuard) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.unambiguous_tls_session_context_id",
                                    "false");
  offerSessionForConcatenatedNames(/*expect_resumption=*/true);
}

// A failed selection fails the handshake even when the client offers a session that the
// certificates of the listener would resume.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, FailedSelectionDoesNotResumeSession) {
  initializeWithDynamicCertificates({});
  ClientSslTransportOptions options;
  options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  // The module lets Envoy select a certificate of the listener for the "www." prefix.
  expectServedCertificate(
      makeSslClientConnection(ClientSslTransportOptions(options).setSni("www.lyft.com")),
      TEST_SERVER_ECDSA_CERT_HASH);
  bssl::UniquePtr<SSL_SESSION> session(SSL_get1_session(clientSsl(*codec_client_->connection())));
  ASSERT_NO_FATAL_FAILURE(expectResumableSession(session.get()));

  // The selector fails a handshake without a server name.
  Network::ClientConnectionPtr connection = makeSslClientConnection(options);
  ASSERT_EQ(1, SSL_set_session(clientSsl(*connection), session.get()));
  expectFailedHandshake(std::move(connection));
  test_server_->waitForCounter(listenerStatPrefix("ssl.connection_error"), testing::Ge(1));
  waitForModuleCounter("selected.outcome.failed", 1);
  EXPECT_EQ(0U, test_server_->counter(listenerStatPrefix("ssl.session_reused"))->value());
}

TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, DeclinedDynamicCertificateFails) {
  initializeWithDynamicCertificates({});
  expectFailedHandshake(ClientSslTransportOptions().setSni("unknown.lyft.com"));
  test_server_->waitForCounter(listenerStatPrefix("ssl.connection_error"), testing::Ge(1));
  waitForModuleCounter("requested", 1);
}

// The client gives up on a handshake that waits, and the certificate that the module provides
// later serves the next handshake.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, PendingHandshakeClientDisconnects) {
  initializeWithDynamicCertificates({dynamicCertificate("hold", "hold.lyft.com", "server2")});
  Network::ClientConnectionPtr connection = startHandshake("hold.lyft.com");
  waitForModuleCounter("requested", 1, dispatcher_.get());
  connection->close(Network::ConnectionCloseType::NoFlush);
  test_server_->waitForGauge(listenerStatPrefix("downstream_cx_active"), testing::Eq(0));

  // The module provides the held certificate when the release name is requested.
  expectFailedHandshake(ClientSslTransportOptions().setSni("release.lyft.com"));
  expectServedCertificate("hold.lyft.com", TEST_SERVER2_CERT_HASH);
  waitForModuleCounter("requested", 2);
}

// A handshake that waits for a certificate that the module never provides ends with the transport
// socket connect timeout.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, PendingHandshakeTimesOut) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_filter_chains(0)
        ->mutable_transport_socket_connect_timeout()
        ->set_seconds(1);
  });
  initializeWithDynamicCertificates({dynamicCertificate("hold", "hold.lyft.com", "server2")});
  Network::ClientConnectionPtr connection = startHandshake("hold.lyft.com");
  waitForModuleCounter("requested", 1, dispatcher_.get());
  test_server_->waitForCounter(listenerStatPrefix("downstream_cx_transport_socket_connect_timeout"),
                               testing::Eq(1), TestUtility::DefaultTimeout, dispatcher_.get());
  connection->close(Network::ConnectionCloseType::NoFlush);
}

// A request that the module does not answer in time fails its handshake, and the next handshake
// requests the certificate again.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, CertificateRequestTimesOut) {
  initializeWithDynamicCertificates({dynamicCertificate("hold", "hold.lyft.com", "server2")},
                                    "dynamic_certificate_request_timeout: 0.2s");
  expectFailedHandshake(ClientSslTransportOptions().setSni("hold.lyft.com"));
  test_server_->waitForCounter(listenerStatPrefix("dynamic_certificates.request_timeout"),
                               testing::Eq(1));
  expectFailedHandshake(ClientSslTransportOptions().setSni("hold.lyft.com"));
  test_server_->waitForCounter(listenerStatPrefix("dynamic_certificates.request_timeout"),
                               testing::Eq(2));
  waitForModuleCounter("requested", 2);
}

// The listener warms until the module provides its certificates and signals that its
// initialization is complete, so the first handshake does not wait for the module.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, ListenerWaitsForModuleInitialization) {
  initializeWithDynamicCertificates({dynamicCertificate("init", "init.lyft.com", "server2")});
  expectServedCertificate("init.lyft.com", TEST_SERVER2_CERT_HASH);
  EXPECT_EQ(0U, test_server_->counter("dynamicmodulescustom.requested")->value());
}

// A module that signals while its configuration is created does not keep the listener warming.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, ModuleSignalsDuringConfigCreation) {
  initializeWithDynamicCertificates(
      {dynamicCertificate("prefetch", "prefetch.lyft.com", "server2")});
  expectServedCertificate("prefetch.lyft.com", TEST_SERVER2_CERT_HASH);
}

// The listener only accepts the RSA client certificate, and so does its dynamic certificate.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest,
       DynamicCertificateValidatesClientCertificate) {
  initializeWithDynamicCertificates(
      {dynamicCertificate("prefetch", "prefetch.lyft.com", "server2")});
  expectServedCertificate("prefetch.lyft.com", TEST_SERVER2_CERT_HASH);
  expectFailedHandshake(
      ClientSslTransportOptions().setSni("prefetch.lyft.com").setClientEcdsaCert(true));
  test_server_->waitForCounter(listenerStatPrefix("ssl.fail_verify_cert_hash"), testing::Ge(1));
}

// The dynamic certificate uses the validation context that SDS delivers, and is rebuilt when SDS
// updates it.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, DynamicCertificateFollowsSdsValidation) {
  writeValidationContext("1", TEST_CLIENT_CERT_HASH);
  useValidationContextFromSds();
  initializeWithDynamicCertificates(
      {dynamicCertificate("prefetch", "prefetch.lyft.com", "server2")});
  expectServedCertificate("prefetch.lyft.com", TEST_SERVER2_CERT_HASH);

  writeValidationContext("2", TEST_CLIENT_ECDSA_CERT_HASH);
  test_server_->waitForCounter("sds.validation.update_success", testing::Eq(2));
  expectFailedHandshake(ClientSslTransportOptions().setSni("prefetch.lyft.com"));
  test_server_->waitForCounter(listenerStatPrefix("ssl.fail_verify_cert_hash"), testing::Ge(1));
  expectServedCertificate("prefetch.lyft.com", TEST_SERVER2_CERT_HASH,
                          ClientSslTransportOptions().setClientEcdsaCert(true));
}

// With session ticket keys, the session of a dynamic certificate resumes until SDS updates the
// validation context, and then the client certificate is verified again. The configured keys
// survive the rebuild of the TLS context, so only the session ID context can stop the resumption.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest,
       ValidationUpdateEndsSessionsOfDynamicCertificate) {
  writeValidationContext("1", TEST_CLIENT_CERT_HASH);
  useValidationContextFromSds();
  modifyListenerTlsContext(
      [](envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& tls_context) {
        tls_context.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(
            std::string(80, 'k'));
      });
  initializeWithDynamicCertificates(
      {dynamicCertificate("prefetch", "prefetch.lyft.com", "server2")});
  Network::UpstreamTransportSocketFactoryPtr client_factory =
      createClientFactory(ClientSslTransportOptions().setSni("prefetch.lyft.com"));
  expectResumedSession(*client_factory, TEST_SERVER2_CERT_HASH);

  writeValidationContext("2", TEST_CLIENT_ECDSA_CERT_HASH);
  test_server_->waitForCounter("sds.validation.update_success", testing::Eq(2));
  Network::ClientConnectionPtr connection = makeSslClientConnectionFromFactory(*client_factory);
  // The client offers the ticket of the resumed session.
  ASSERT_NO_FATAL_FAILURE(expectResumableSession(SSL_get_session(clientSsl(*connection))));
  expectFailedHandshake(std::move(connection));
  test_server_->waitForCounter(listenerStatPrefix("ssl.fail_verify_cert_hash"), testing::Ge(1));
  EXPECT_EQ(1U, test_server_->counter(listenerStatPrefix("ssl.session_reused"))->value());
}

// The configured certificates have no OCSP response, so the stapled one comes from the dynamic
// certificate. It is prefetched, since OpenSSL builds do not staple the response of a selection
// that completes asynchronously.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, StaplesDynamicCertificateOcspResponse) {
  initializeWithDynamicCertificates(
      {dynamicCertificate("prefetch", "ocsp.lyft.com", "server", true)});
  Network::ClientConnectionPtr connection =
      makeSslClientConnection(ClientSslTransportOptions().setSni("ocsp.lyft.com"));
  SSL* ssl = clientSsl(*connection);
  SSL_enable_ocsp_stapling(ssl);
  codec_client_ = makeHttpConnection(std::move(connection));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(certificateDigest(TEST_SERVER_CERT_HASH),
            codec_client_->connection()->ssl()->sha256PeerCertificateDigest());
  const uint8_t* ocsp_response = nullptr;
  size_t ocsp_response_length = 0;
  SSL_get0_ocsp_response(ssl, &ocsp_response, &ocsp_response_length);
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
                "test/config/integration/certs/server_ocsp_resp.der")),
            std::string(reinterpret_cast<const char*>(ocsp_response), ocsp_response_length));
  codec_client_->close();
}

// The expiration of the first OCSP response resets once the module replaces the only certificate
// with an OCSP response by one without.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest,
       ReplacingStapledCertificateResetsOcspResponseExpiration) {
  flushStatsOften();
  initializeWithDynamicCertificates(
      {dynamicCertificate("prefetch", "ocsp.lyft.com", "server", true),
       dynamicCertificate("hold", "ocsp.lyft.com", "server")});
  waitForOcspResponseExpiration(testing::Gt(0));
  // The module provides the held certificate when the release name is requested.
  expectFailedHandshake(ClientSslTransportOptions().setSni("release.lyft.com"));
  waitForOcspResponseExpiration(testing::Eq(0));
}

// The expiration of the first OCSP response resets once the module removes the only certificate
// with an OCSP response.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest,
       RemovingStapledCertificateResetsOcspResponseExpiration) {
  flushStatsOften();
  initializeWithDynamicCertificates(
      {dynamicCertificate("prefetch", "ocsp.lyft.com", "server", true)});
  waitForOcspResponseExpiration(testing::Gt(0));
  // The module removes the certificate whose name follows the "remove." prefix.
  expectFailedHandshake(ClientSslTransportOptions().setSni("remove.ocsp.lyft.com"));
  waitForOcspResponseExpiration(testing::Eq(0));
}

// An expired dynamic certificate sets the days until the first certificate expires to 0, and
// replacing it restores the days of the other certificates. They can be one less if a day passes
// while the test runs.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest,
       ReplacingExpiredCertificateRestoresCertificateExpiration) {
  flushStatsOften();
  initializeWithDynamicCertificates({dynamicCertificate("sync", "expired.lyft.com", "expired_"),
                                     dynamicCertificate("hold", "expired.lyft.com", "server2")});
  waitForCertificateExpiration(testing::Gt(0));
  const uint64_t initial_days =
      test_server_->gauge("server.days_until_first_cert_expiring")->value();
  // The client rejects the expired certificate, which the module provides when it is requested.
  expectFailedHandshake(ClientSslTransportOptions().setSni("expired.lyft.com"));
  waitForCertificateExpiration(testing::Eq(0));
  // The module provides the held certificate when the release name is requested.
  expectFailedHandshake(ClientSslTransportOptions().setSni("release.lyft.com"));
  waitForCertificateExpiration(testing::AllOf(testing::Gt(0), testing::Le(initial_days)));
}

// An expired dynamic certificate sets the days until the first certificate expires to 0, and
// removing it restores the days of the other certificates. They can be one less if a day passes
// while the test runs.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest,
       RemovingExpiredCertificateRestoresCertificateExpiration) {
  flushStatsOften();
  initializeWithDynamicCertificates({dynamicCertificate("hold", "expired.lyft.com", "expired_")});
  waitForCertificateExpiration(testing::Gt(0));
  const uint64_t initial_days =
      test_server_->gauge("server.days_until_first_cert_expiring")->value();
  // The module provides the held certificate when the release name is requested.
  expectFailedHandshake(ClientSslTransportOptions().setSni("release.lyft.com"));
  waitForCertificateExpiration(testing::Eq(0));
  // The module removes the certificate whose name follows the "remove." prefix.
  expectFailedHandshake(ClientSslTransportOptions().setSni("remove.expired.lyft.com"));
  waitForCertificateExpiration(testing::AllOf(testing::Gt(0), testing::Le(initial_days)));
}

// The dynamic certificates are listed by /certs without the trusted certificate, which only the TLS
// context lists, and the selector tracks them in its stats.
TEST_P(DynamicModuleTlsCertificateSelectorIntegrationTest, ReportsDynamicCertificates) {
  DISABLE_IF_ADMIN_DISABLED;
  initializeWithDynamicCertificates({dynamicCertificate("prefetch", "prefetch.lyft.com", "server2"),
                                     dynamicCertificate("hold", "hold.lyft.com", "server2")});
  test_server_->waitForGauge(listenerStatPrefix("dynamic_certificates.active"), testing::Eq(1));
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/certs", "", downstreamProtocol(), version_);
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Only the dynamic certificate is for lyft2.com.
  EXPECT_THAT(response->body(), testing::HasSubstr("lyft2.com"));
  envoy::admin::v3::Certificates certificates;
  TestUtility::loadFromJson(response->body(), certificates);
  EXPECT_EQ(2, certificates.certificates_size());
  EXPECT_EQ(1, std::count_if(certificates.certificates().begin(), certificates.certificates().end(),
                             [](const envoy::admin::v3::Certificate& certificate) {
                               return !certificate.ca_cert().empty();
                             }));

  Network::ClientConnectionPtr connection = startHandshake("hold.lyft.com");
  waitForModuleCounter("requested", 1, dispatcher_.get());
  EXPECT_EQ(1U, test_server_->gauge(listenerStatPrefix("dynamic_certificates.pending"))->value());
  connection->close(Network::ConnectionCloseType::NoFlush);

  // The module provides the held certificate when the release name is requested.
  expectFailedHandshake(ClientSslTransportOptions().setSni("release.lyft.com"));
  test_server_->waitForGauge(listenerStatPrefix("dynamic_certificates.pending"), testing::Eq(0));
  test_server_->waitForGauge(listenerStatPrefix("dynamic_certificates.active"), testing::Eq(2));
  EXPECT_EQ(2U,
            test_server_->counter(listenerStatPrefix("dynamic_certificates.requested"))->value());
  EXPECT_EQ(2U,
            test_server_->counter(listenerStatPrefix("dynamic_certificates.provided"))->value());
}

class DynamicModuleUpstreamTlsCertificateSelectorIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleUpstreamTlsCertificateSelectorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  // Initializes the cluster with the selector config and the given client certificates.
  void initializeWithSelector(const std::string& selector_config,
                              const std::vector<std::string>& client_certificates) {
    setModulesSearchPath();
    upstream_tls_ = true;
    config_helper_.configureUpstreamTls(
        false, false, std::nullopt,
        [selector_config, client_certificates](
            envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& tls_context) {
          for (const std::string& name : client_certificates) {
            auto* tls_certificate =
                tls_context.mutable_common_tls_context()->add_tls_certificates();
            tls_certificate->mutable_certificate_chain()->set_filename(
                TestEnvironment::runfilesPath(
                    absl::StrCat("test/config/integration/certs/", name, "cert.pem")));
            tls_certificate->mutable_private_key()->set_filename(TestEnvironment::runfilesPath(
                absl::StrCat("test/config/integration/certs/", name, "key.pem")));
          }
          // Multiple and dynamic client certificates require session resumption to be disabled.
          tls_context.mutable_max_session_keys()->set_value(0);
          TestUtility::loadFromYaml(
              selector_config,
              *tls_context.mutable_common_tls_context()->mutable_custom_tls_certificate_selector());
        });
    HttpIntegrationTest::initialize();
  }

  // Sends a request and expects the upstream to receive the client certificate with the given
  // hash. The upstream connection is then closed, so the next request makes a new handshake.
  void expectClientCertificate(absl::string_view certificate_hash,
                               const Http::TestRequestHeaderMapImpl& request_headers) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();
    EXPECT_EQ(certificateDigest(certificate_hash),
              fake_upstream_connection_->connection().ssl()->sha256PeerCertificateDigest());
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    fake_upstream_connection_.reset();
    test_server_->waitForGauge("cluster.cluster_0.upstream_cx_active", testing::Eq(0));
    codec_client_->close();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleUpstreamTlsCertificateSelectorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModuleUpstreamTlsCertificateSelectorIntegrationTest,
       SelectsClientCertificateForServerName) {
  // The cluster sends the request authority as the server name.
  initializeWithSelector(selectorConfig("upstream", default_request_headers_.getHostValue()),
                         {"client", "client_ecdsa"});
  expectClientCertificate(TEST_CLIENT_ECDSA_CERT_HASH, default_request_headers_);
}

// The downstream request shares the filter state that selects the client certificate.
TEST_P(DynamicModuleUpstreamTlsCertificateSelectorIntegrationTest,
       SelectsClientCertificateForFilterState) {
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_filter_state.v3.Config
  on_request_headers:
  - object_key: tenant
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "%REQ(x-tenant)%"
    shared_with_upstream: ONCE
)EOF");
  initializeWithSelector(selectorConfig("filter_state", "lyft"), {"client", "client_ecdsa"});
  expectClientCertificate(TEST_CLIENT_CERT_HASH, default_request_headers_);
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("x-tenant", "lyft");
  expectClientCertificate(TEST_CLIENT_ECDSA_CERT_HASH, request_headers);
}

TEST_P(DynamicModuleUpstreamTlsCertificateSelectorIntegrationTest,
       FailedSelectionFailsUpstreamHandshake) {
  initializeWithSelector(selectorConfig("fail", default_request_headers_.getHostValue()),
                         {"client", "client_ecdsa"});
  // The test does not wait for the upstream connection, so the fake upstream reads it on its own.
  fake_upstreams_[0]->setReadDisableOnNewConnection(false);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.ssl.connection_error", testing::Ge(1));
}

// The upstream handshake waits while the module fetches the client certificate on another thread.
TEST_P(DynamicModuleUpstreamTlsCertificateSelectorIntegrationTest, ServesDynamicClientCertificate) {
  initializeWithSelector(dynamicSelectorConfig({dynamicCertificate(
                             "fetch", default_request_headers_.getHostValue(), "client_ecdsa")}),
                         {});
  expectClientCertificate(TEST_CLIENT_ECDSA_CERT_HASH, default_request_headers_);
  test_server_->waitForCounter("dynamicmodulescustom.requested", testing::Eq(1));
  test_server_->waitForCounter("cluster.cluster_0.dynamic_certificates.provided", testing::Eq(1));
  test_server_->waitForGauge("cluster.cluster_0.dynamic_certificates.active", testing::Eq(1));
}

// The cluster warms until the module provides its client certificate and signals that its
// initialization is complete, so the first upstream handshake does not wait for the module.
TEST_P(DynamicModuleUpstreamTlsCertificateSelectorIntegrationTest,
       ClusterWaitsForModuleInitialization) {
  initializeWithSelector(dynamicSelectorConfig({dynamicCertificate(
                             "init", default_request_headers_.getHostValue(), "client_ecdsa")}),
                         {});
  expectClientCertificate(TEST_CLIENT_ECDSA_CERT_HASH, default_request_headers_);
  EXPECT_EQ(0U, test_server_->counter("dynamicmodulescustom.requested")->value());
}

} // namespace
} // namespace Ssl
} // namespace Envoy
