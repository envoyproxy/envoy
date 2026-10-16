#include <array>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "envoy/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"

#include "source/common/config/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_context_impl.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/common/async_context.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/config.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/tls_certificate_selector.h"

#include "test/common/tls/ssl_certs_test.h"
#include "test/common/tls/ssl_test_utility.h"
#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace DynamicModules {

class DynamicModuleTlsCertificateSelectorConfigPeer {
public:
  static size_t waitingHandleCount(const DynamicModuleTlsCertificateSelectorConfig& config,
                                   const std::string& name) {
    return config.dynamic_certificates_.at(name).waiting_handles.size();
  }
};

namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;

using DownstreamTlsContext = envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext;
using UpstreamTlsContext = envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext;
using OcspStaplePolicy = Ssl::ServerContextConfig::OcspStaplePolicy;
using SelectionStatus = Ssl::SelectionResult::SelectionStatus;

// The status_request extension of a ClientHello, which marks the client as OCSP capable.
constexpr uint8_t StatusRequestExtension[] = {0x00, 0x05, 0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00};

// The ALPN extension of a ClientHello that offers h2.
constexpr uint8_t AlpnExtension[] = {0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 'h', '2'};

std::string certificatePath(absl::string_view name) {
  return TestEnvironment::substitute(
      absl::StrCat("{{ test_rundir }}/test/config/integration/certs/", name));
}

std::string readCertificateFile(absl::string_view name) {
  return TestEnvironment::readFileToStringForTest(certificatePath(name));
}

// The certificate chain and the private key of a test certificate in one PEM blob.
std::string certificateAndKeyPem(absl::string_view name) {
  return readCertificateFile(absl::StrCat(name, "cert.pem")) +
         readCertificateFile(absl::StrCat(name, "key.pem"));
}

void addCertificate(envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& tls_context,
                    absl::string_view name, absl::string_view ocsp_staple = "") {
  auto* certificate = tls_context.add_tls_certificates();
  certificate->mutable_certificate_chain()->set_filename(
      certificatePath(absl::StrCat(name, "cert.pem")));
  certificate->mutable_private_key()->set_filename(certificatePath(absl::StrCat(name, "key.pem")));
  if (!ocsp_staple.empty()) {
    certificate->mutable_ocsp_staple()->set_filename(certificatePath(ocsp_staple));
  }
}

// A downstream TLS context with the server and server2 certificates.
DownstreamTlsContext downstreamTlsContext() {
  DownstreamTlsContext tls_context;
  addCertificate(*tls_context.mutable_common_tls_context(), "server");
  addCertificate(*tls_context.mutable_common_tls_context(), "server2");
  return tls_context;
}

DynamicModuleTlsCertificateSelectorProto selectorProto(const std::string& module_name,
                                                       const std::string& selector_config = "") {
  DynamicModuleTlsCertificateSelectorProto proto_config;
  proto_config.mutable_dynamic_module_config()->set_name(module_name);
  proto_config.set_selector_name("selector");
  if (!selector_config.empty()) {
    Protobuf::StringValue value;
    value.set_value(selector_config);
    EXPECT_TRUE(proto_config.mutable_selector_config()->PackFrom(value));
  }
  return proto_config;
}

// Returns true if the TLS context serves the certificate of the given name.
bool servesCertificate(const Ssl::TlsContext* tls_context, absl::string_view name) {
  const bssl::UniquePtr<X509> certificate =
      readCertFromFile(certificatePath(absl::StrCat(name, "cert.pem")));
  return tls_context != nullptr && tls_context->cert_chain_ != nullptr &&
         X509_cmp(tls_context->cert_chain_.get(), certificate.get()) == 0;
}

envoy_dynamic_module_type_module_buffer moduleBuffer(absl::string_view value) {
  return {value.data(), value.size()};
}

class MockCertificateSelectionCallback : public Ssl::CertificateSelectionCallback {
public:
  explicit MockCertificateSelectionCallback(Event::Dispatcher& dispatcher) {
    EXPECT_CALL(*this, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  }

  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, onCertificateSelectionResult, (OptRef<const Ssl::TlsContext>, bool));
};

// A dynamic certificate whose OCSP stapling policy always fails.
class MustStapleAsyncContext : public AsyncContext {
public:
  explicit MustStapleAsyncContext(Stats::Scope& scope) : AsyncContext(scope) {}

  Ssl::ServerContextConfig::OcspStaplePolicy ocspStaplePolicy() const override {
    return OcspStaplePolicy::MustStaple;
  }
  const Ssl::TlsContext& tlsContext() const override { return tls_context_; }

private:
  Ssl::TlsContext tls_context_;
};

class DynamicModuleTlsCertificateSelectorTest : public SslCertsTest {
protected:
  DynamicModuleTlsCertificateSelectorTest() {
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();
  }

  // Creates the downstream TLS context and a selector of the module for it.
  void initializeDownstream(const std::string& module_name,
                            const DownstreamTlsContext& tls_context = downstreamTlsContext(),
                            bool allow_dynamic_certificates = false) {
    auto server_config = ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
    ASSERT_TRUE(server_config.ok()) << server_config.status();
    server_config_ = std::move(*server_config);
    auto server_context = ServerContextImpl::create(
        *store_.rootScope(), *server_config_, factory_context_.serverFactoryContext(), nullptr);
    ASSERT_TRUE(server_context.ok()) << server_context.status();
    server_context_ = std::move(*server_context);
    useDownstreamConfig(
        createConfig(module_name, *server_config_,
                     allow_dynamic_certificates ? serverContextFactory() : nullptr));
  }

  // Replaces the configuration of the downstream selector.
  void useDownstreamConfig(DynamicModuleTlsCertificateSelectorConfigSharedPtr config) {
    ASSERT_NE(nullptr, config);
    config_ = std::move(config);
    factory_ =
        std::make_unique<DynamicModuleTlsCertificateSelectorFactory>(config_, *server_config_);
    selector_ = factory_->create(*server_context_);
  }

  // Creates the upstream TLS context and a selector of the module for it. The module is loaded
  // through the config factory, since an upstream TLS context with several certificates requires a
  // certificate selector.
  void initializeUpstream(UpstreamTlsContext tls_context, const std::string& module_name) {
    auto* selector_config =
        tls_context.mutable_common_tls_context()->mutable_custom_tls_certificate_selector();
    selector_config->set_name("envoy.tls.certificate_selectors.dynamic_modules");
    ASSERT_TRUE(selector_config->mutable_typed_config()->PackFrom(selectorProto(module_name)));
    tls_context.mutable_max_session_keys()->set_value(0);
    createUpstreamContext(tls_context);
    upstream_selector_ =
        client_config_->tlsCertificateSelectorFactory()->createUpstreamTlsCertificateSelector(
            *client_context_);
  }

  // Creates the upstream TLS context with the client certificate, and a selector of the module
  // that allows dynamic certificates.
  void initializeUpstreamWithDynamicCertificates() {
    UpstreamTlsContext tls_context;
    addCertificate(*tls_context.mutable_common_tls_context(), "client");
    createUpstreamContext(tls_context);
    config_ = createConfig(
        "tls_certificate_selector_dynamic", *client_config_,
        [this](Stats::Scope& scope, Server::Configuration::ServerFactoryContext& server_context,
               const Ssl::TlsCertificateConfig& certificate_config,
               absl::Status& creation_status) -> AsyncContextConstSharedPtr {
          return std::make_shared<ClientAsyncContext>(scope, server_context, *client_config_,
                                                      certificate_config, creation_status);
        },
        true);
    ASSERT_NE(nullptr, config_);
    upstream_factory_ =
        std::make_unique<DynamicModuleUpstreamTlsCertificateSelectorFactory>(config_);
    upstream_selector_ = upstream_factory_->createUpstreamTlsCertificateSelector(*client_context_);
  }

  void createUpstreamContext(const UpstreamTlsContext& tls_context) {
    auto client_config = ClientContextConfigImpl::create(tls_context, factory_context_);
    ASSERT_TRUE(client_config.ok()) << client_config.status();
    client_config_ = std::move(*client_config);
    auto client_context = ClientContextImpl::create(*store_.rootScope(), *client_config_,
                                                    factory_context_.serverFactoryContext());
    ASSERT_TRUE(client_context.ok()) << client_context.status();
    client_context_ = std::move(*client_context);
  }

  DynamicModuleTlsCertificateSelectorConfigSharedPtr
  createConfig(const std::string& module_name, const Ssl::ContextConfig& tls_config,
               AsyncContextFactory dynamic_context_factory, bool is_upstream = false,
               absl::string_view selector_config = "") {
    auto module =
        Envoy::Extensions::DynamicModules::newDynamicModuleByName(module_name, false, false);
    EXPECT_TRUE(module.ok()) << module.status();
    auto config = newDynamicModuleTlsCertificateSelectorConfig(
        "selector", selector_config, is_upstream, DefaultMetricsNamespace,
        std::move(module.value()), factory_context_, tls_config,
        std::move(dynamic_context_factory));
    EXPECT_TRUE(config.ok()) << config.status();
    return config.ok() ? *config : nullptr;
  }

  AsyncContextFactory serverContextFactory() {
    return [this](Stats::Scope& scope, Server::Configuration::ServerFactoryContext& server_context,
                  const Ssl::TlsCertificateConfig& certificate_config,
                  absl::Status& creation_status) -> AsyncContextConstSharedPtr {
      return std::make_shared<ServerAsyncContext>(scope, server_context, *server_config_,
                                                  certificate_config, creation_status);
    };
  }

  // Selects a certificate for a ClientHello with the given server name and extensions.
  Ssl::SelectionResult selectDownstream(const char* server_name,
                                        absl::Span<const uint8_t> extensions = {},
                                        Ssl::CertificateSelectionCallbackPtr cb = nullptr) {
    if (server_name != nullptr) {
      EXPECT_EQ(1, SSL_set_tlsext_host_name(ssl_.get(), server_name));
    }
    SSL_CLIENT_HELLO client_hello{};
    client_hello.ssl = ssl_.get();
    client_hello.extensions = extensions.data();
    client_hello.extensions_len = extensions.size();
    return selector_->selectTlsContext(client_hello, std::move(cb));
  }

  Ssl::SelectionResult selectUpstream(const char* server_name) {
    EXPECT_EQ(1, SSL_set_tlsext_host_name(ssl_.get(), server_name));
    return upstream_selector_->selectTlsContext(*ssl_, nullptr, nullptr);
  }

  std::unique_ptr<StrictMock<MockCertificateSelectionCallback>> newCallback() {
    return std::make_unique<StrictMock<MockCertificateSelectionCallback>>(worker_dispatcher_);
  }

  bool setCertificate(absl::string_view name, absl::string_view certificate,
                      absl::string_view ocsp_staple = "") {
    return setCertificatePem(name, readCertificateFile(absl::StrCat(certificate, "cert.pem")),
                             readCertificateFile(absl::StrCat(certificate, "key.pem")),
                             ocsp_staple.empty() ? "" : readCertificateFile(ocsp_staple));
  }

  bool setCertificatePem(absl::string_view name, absl::string_view certificate_chain,
                         absl::string_view private_key, absl::string_view ocsp_staple = "") {
    return envoy_dynamic_module_callback_tls_certificate_selector_config_set_certificate(
        config_.get(), moduleBuffer(name), moduleBuffer(certificate_chain),
        moduleBuffer(private_key), moduleBuffer(ocsp_staple));
  }

  void removeCertificate(absl::string_view name) {
    envoy_dynamic_module_callback_tls_certificate_selector_config_remove_certificate(
        config_.get(), moduleBuffer(name));
  }

  // The module metrics are in the server scope.
  uint64_t counterValue(const std::string& name) {
    const auto counter = TestUtility::findCounter(factory_context_.server_context_.store_, name);
    EXPECT_NE(nullptr, counter) << name;
    return counter != nullptr ? counter->value() : 0;
  }

  std::unique_ptr<ServerContextConfigImpl> server_config_;
  std::unique_ptr<ServerContextImpl> server_context_;
  std::unique_ptr<ClientContextConfigImpl> client_config_;
  std::unique_ptr<ClientContextImpl> client_context_;
  StrictMock<Ssl::MockServerContextConfig> mock_tls_config_;
  DynamicModuleTlsCertificateSelectorConfigSharedPtr config_;
  std::unique_ptr<DynamicModuleTlsCertificateSelectorFactory> factory_;
  std::unique_ptr<DynamicModuleUpstreamTlsCertificateSelectorFactory> upstream_factory_;
  Ssl::TlsCertificateSelectorPtr selector_;
  Ssl::UpstreamTlsCertificateSelectorPtr upstream_selector_;
  NiceMock<Event::MockDispatcher> worker_dispatcher_;
  bssl::UniquePtr<SSL_CTX> ssl_ctx_{SSL_CTX_new(TLS_method())};
  bssl::UniquePtr<SSL> ssl_{SSL_new(ssl_ctx_.get())};
};

TEST_F(DynamicModuleTlsCertificateSelectorTest, SelectsCertificateByServerName) {
  initializeDownstream("tls_certificate_selector_by_server_name");
  const Ssl::SelectionResult result = selectDownstream("www.lyft2.com");
  EXPECT_EQ(SelectionStatus::Success, result.status);
  EXPECT_EQ(&server_context_->getTlsContexts()[1], result.selected_ctx);
  EXPECT_FALSE(result.staple);
  EXPECT_FALSE(selector_->providesCertificates());
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, SelectsCertificateWithoutServerName) {
  initializeDownstream("tls_certificate_selector_by_server_name");
  EXPECT_EQ(&server_context_->getTlsContexts()[1], selectDownstream(nullptr).selected_ctx);
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, ModuleFailureFailsSelection) {
  initializeDownstream("tls_certificate_selector_by_server_name");
  const Ssl::SelectionResult result = selectDownstream("www.example.com");
  EXPECT_EQ(SelectionStatus::Failed, result.status);
  EXPECT_EQ(nullptr, result.selected_ctx);
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, OutOfRangeIndexFailsSelection) {
  initializeDownstream("tls_certificate_selector_out_of_range");
  EXPECT_LOG_CONTAINS(
      "debug", "dynamic module selected TLS certificate index 2, but the TLS context has 2",
      { EXPECT_EQ(SelectionStatus::Failed, selectDownstream("www.lyft.com").status); });
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, SelectorCreationFailureFailsSelection) {
  EXPECT_LOG_CONTAINS("error", "dynamic module failed to create a TLS certificate selector",
                      { initializeDownstream("tls_certificate_selector_new_fail"); });
  EXPECT_EQ(SelectionStatus::Failed, selectDownstream("www.lyft.com").status);
  DynamicModuleUpstreamTlsCertificateSelectorFactory upstream_factory(config_);
  EXPECT_EQ(SelectionStatus::Failed,
            upstream_factory.createUpstreamTlsCertificateSelector(*server_context_)
                ->selectTlsContext(*ssl_, nullptr, nullptr)
                .status);
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, StaplesOcspResponseForCapableClient) {
  DownstreamTlsContext tls_context;
  addCertificate(*tls_context.mutable_common_tls_context(), "server", "server_ocsp_resp.der");
  addCertificate(*tls_context.mutable_common_tls_context(), "server2");
  initializeDownstream("tls_certificate_selector_by_server_name", tls_context);

  const Ssl::SelectionResult result = selectDownstream("www.lyft.com", StatusRequestExtension);
  EXPECT_EQ(SelectionStatus::Success, result.status);
  EXPECT_EQ(&server_context_->getTlsContexts()[0], result.selected_ctx);
  EXPECT_TRUE(result.staple);

  // A client that does not request OCSP stapling gets no staple.
  EXPECT_FALSE(selectDownstream("www.lyft.com").staple);
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, OcspPolicyFailureFailsSelection) {
  initializeDownstream("tls_certificate_selector_by_server_name");
  DynamicModuleTlsCertificateSelector selector(config_, *server_context_, nullptr,
                                               OcspStaplePolicy::MustStaple);
  ASSERT_EQ(1, SSL_set_tlsext_host_name(ssl_.get(), "www.lyft2.com"));
  SSL_CLIENT_HELLO client_hello{};
  client_hello.ssl = ssl_.get();
  client_hello.extensions = StatusRequestExtension;
  client_hello.extensions_len = sizeof(StatusRequestExtension);
  EXPECT_LOG_CONTAINS("debug", "fails the OCSP policy", {
    EXPECT_EQ(SelectionStatus::Failed, selector.selectTlsContext(client_hello, nullptr).status);
  });
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, FindTlsContextIsNotImplemented) {
  initializeDownstream("tls_certificate_selector_by_server_name");
  bool cert_matched_sni = false;
  EXPECT_DEATH(selector_->findTlsContext("www.lyft.com", {}, false, &cert_matched_sni),
               "not implemented");
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, DefaultLetsEnvoySelectByServerName) {
  initializeDownstream("tls_certificate_selector_dynamic");
  // The module serves the first certificate for this name, which Envoy would not select.
  EXPECT_EQ(&server_context_->getTlsContexts()[0], selectDownstream("lyft2.com").selected_ctx);
  EXPECT_EQ(&server_context_->getTlsContexts()[1], selectDownstream("www.lyft2.com").selected_ctx);
  EXPECT_EQ(1U, counterValue("dynamicmodulescustom.selected.outcome.configured"));
  EXPECT_EQ(1U, counterValue("dynamicmodulescustom.selected.outcome.default"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, DynamicCertificatesNotAllowed) {
  initializeDownstream("tls_certificate_selector_dynamic");
  EXPECT_FALSE(selector_->providesCertificates());
  EXPECT_LOG_CONTAINS("debug", "dynamic certificates are not allowed", {
    EXPECT_EQ(SelectionStatus::Failed, selectDownstream("dynamic.lyft.com").status);
  });
  EXPECT_LOG_CONTAINS("warn", "dynamic certificates are not allowed",
                      { EXPECT_FALSE(setCertificate("dynamic.lyft.com", "server2")); });
  // Removing is a no-op when dynamic certificates are not allowed.
  removeCertificate("dynamic.lyft.com");
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, ServesProvidedDynamicCertificate) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  EXPECT_TRUE(selector_->providesCertificates());
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));

  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com");
  EXPECT_EQ(SelectionStatus::Success, result.status);
  EXPECT_TRUE(servesCertificate(result.selected_ctx, "server2"));
  EXPECT_FALSE(result.staple);
  ASSERT_NE(nullptr, result.handle);

  // The handle keeps the certificate alive for the handshake after it is replaced and removed.
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server"));
  EXPECT_TRUE(servesCertificate(selectDownstream("dynamic.lyft.com").selected_ctx, "server"));
  removeCertificate("dynamic.lyft.com");
  EXPECT_TRUE(servesCertificate(result.selected_ctx, "server2"));
  EXPECT_EQ(0U, counterValue("dynamicmodulescustom.requested"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, HandshakeWaitsForDynamicCertificate) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  std::vector<Ssl::SelectionResult> results;
  for (int i = 0; i < 4; i++) {
    auto callback = newCallback();
    EXPECT_CALL(*callback, onCertificateSelectionResult(_, false))
        .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
          EXPECT_TRUE(servesCertificate(tls_context.ptr(), "server2"));
        });
    results.push_back(selectDownstream("dynamic.lyft.com", {}, std::move(callback)));
    EXPECT_EQ(SelectionStatus::Pending, results.back().status);
    EXPECT_NE(nullptr, results.back().handle);
  }
  // The module is asked once for each name.
  EXPECT_EQ(1U, counterValue("dynamicmodulescustom.requested"));
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
}

// The handshake resumes on its own dispatcher, which keeps the certificate alive until then, even
// after the handshake and the module drop it.
TEST_F(DynamicModuleTlsCertificateSelectorTest, WaitingHandshakeResumesOnItsDispatcher) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(worker_dispatcher_, post(_)).WillOnce([&posted](Event::PostCb cb) {
    posted.push_back(std::move(cb));
  });
  auto callback = newCallback();
  auto& waiting = *callback;
  Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", {}, std::move(callback));
  ASSERT_EQ(SelectionStatus::Pending, result.status);
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  removeCertificate("dynamic.lyft.com");
  result.handle.reset();

  EXPECT_CALL(waiting, onCertificateSelectionResult(_, false))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_TRUE(servesCertificate(tls_context.ptr(), "server2"));
      });
  ASSERT_EQ(1U, posted.size());
  posted[0]();
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, WaitingHandshakeStaplesOcspResponse) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  auto callback = newCallback();
  EXPECT_CALL(*callback, onCertificateSelectionResult(_, true))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_TRUE(servesCertificate(tls_context.ptr(), "server_ecdsa"));
      });
  const Ssl::SelectionResult result =
      selectDownstream("dynamic.lyft.com", StatusRequestExtension, std::move(callback));
  EXPECT_EQ(SelectionStatus::Pending, result.status);
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server_ecdsa", "server_ecdsa_ocsp_resp.der"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, WaitingHandshakeIsResumedOnce) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  auto callback = newCallback();
  EXPECT_CALL(*callback, onCertificateSelectionResult(_, false));
  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", {}, std::move(callback));
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server"));
  removeCertificate("dynamic.lyft.com");
}

// Without the requested hook, the module is never asked for the certificate, so the handshake fails
// until the module provides it.
TEST_F(DynamicModuleTlsCertificateSelectorTest, DynamicCertificateWithoutRequestedHook) {
  initializeDownstream("tls_certificate_selector_dynamic_without_hooks", downstreamTlsContext(),
                       true);
  EXPECT_LOG_CONTAINS("debug",
                      "dynamic module selected TLS certificate 'dynamic.lyft.com', which it has "
                      "not provided",
                      { EXPECT_EQ(SelectionStatus::Failed, selectDownstream("lyft.com").status); });
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  EXPECT_TRUE(servesCertificate(selectDownstream("lyft.com").selected_ctx, "server2"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, DeclinedDynamicCertificateFailsHandshake) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  auto callback = newCallback();
  EXPECT_CALL(*callback, onCertificateSelectionResult(_, false))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_FALSE(tls_context.has_value());
      });
  // The module removes the certificate from the requested hook.
  EXPECT_EQ(SelectionStatus::Pending,
            selectDownstream("decline.lyft.com", {}, std::move(callback)).status);
  EXPECT_EQ(1U, counterValue("dynamicmodulescustom.requested"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, RemovedDynamicCertificateIsRequestedAgain) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  EXPECT_EQ(SelectionStatus::Success, selectDownstream("dynamic.lyft.com").status);
  removeCertificate("dynamic.lyft.com");
  removeCertificate("dynamic.lyft.com");

  auto callback = newCallback();
  auto& waiting = *callback;
  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", {}, std::move(callback));
  EXPECT_EQ(SelectionStatus::Pending, result.status);
  EXPECT_EQ(1U, counterValue("dynamicmodulescustom.requested"));
  EXPECT_CALL(waiting, onCertificateSelectionResult(_, false))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_FALSE(tls_context.has_value());
      });
  removeCertificate("dynamic.lyft.com");
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, InvalidDynamicCertificateIsRejected) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  const std::string certificate_chain = readCertificateFile("server2cert.pem");
  const std::string private_key = readCertificateFile("server2key.pem");
  EXPECT_LOG_CONTAINS("warn", "dynamic module set an invalid TLS certificate 'dynamic.lyft.com'", {
    EXPECT_FALSE(setCertificatePem("dynamic.lyft.com", "not a certificate", "not a key"));
  });
  EXPECT_LOG_CONTAINS("warn", "certificate chain not set",
                      { EXPECT_FALSE(setCertificatePem("dynamic.lyft.com", "", private_key)); });
  // The private key does not match the certificate.
  EXPECT_FALSE(setCertificatePem("dynamic.lyft.com", certificate_chain,
                                 readCertificateFile("serverkey.pem")));

  // An invalid replacement keeps the current certificate.
  ASSERT_TRUE(setCertificatePem("dynamic.lyft.com", certificate_chain, private_key));
  EXPECT_FALSE(setCertificatePem("dynamic.lyft.com", "not a certificate", private_key));
  EXPECT_TRUE(servesCertificate(selectDownstream("dynamic.lyft.com").selected_ctx, "server2"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, DynamicCertificateStaplesOcspResponse) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server_ecdsa", "server_ecdsa_ocsp_resp.der"));
  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", StatusRequestExtension);
  EXPECT_EQ(SelectionStatus::Success, result.status);
  EXPECT_TRUE(servesCertificate(result.selected_ctx, "server_ecdsa"));
  EXPECT_TRUE(result.staple);
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, WaitingHandshakeFailsOcspPolicy) {
  auto callback = newCallback();
  EXPECT_CALL(*callback, onCertificateSelectionResult(_, false))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_FALSE(tls_context.has_value());
      });
  DynamicCertificateHandle handle(std::move(callback), true);
  handle.notify(std::make_shared<MustStapleAsyncContext>(*store_.rootScope()));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, DynamicCertificatesWaitForValidationContext) {
  initializeDownstream("tls_certificate_selector_dynamic");
  EXPECT_CALL(mock_tls_config_, validationContextConfigured()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_tls_config_, certificateValidationContext()).WillRepeatedly(Return(nullptr));
  useDownstreamConfig(
      createConfig("tls_certificate_selector_dynamic", mock_tls_config_, serverContextFactory()));

  // The certificate is built once the validation context arrives, and a rejected certificate does
  // not replace it.
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  EXPECT_FALSE(setCertificatePem("dynamic.lyft.com", "", readCertificateFile("server2key.pem")));
  auto callback = newCallback();
  auto& waiting = *callback;
  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", {}, std::move(callback));
  EXPECT_EQ(SelectionStatus::Pending, result.status);
  // The module already provided the certificate, so it is not requested.
  EXPECT_EQ(0U, counterValue("dynamicmodulescustom.requested"));
  EXPECT_TRUE(factory_->onConfigUpdate().ok());

  StrictMock<Ssl::MockCertificateValidationContextConfig> validation_context;
  EXPECT_CALL(mock_tls_config_, certificateValidationContext())
      .WillRepeatedly(Return(&validation_context));
  EXPECT_CALL(waiting, onCertificateSelectionResult(_, false))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_TRUE(servesCertificate(tls_context.ptr(), "server2"));
      });
  EXPECT_TRUE(factory_->onConfigUpdate().ok());
  EXPECT_EQ(SelectionStatus::Success, selectDownstream("dynamic.lyft.com").status);
}

// Dynamic certificates replace the certificates of the TLS context, so they do not wait for them.
TEST_F(DynamicModuleTlsCertificateSelectorTest, DynamicCertificatesDoNotWaitForTlsCertificates) {
  initializeDownstream("tls_certificate_selector_dynamic");
  EXPECT_CALL(mock_tls_config_, validationContextConfigured()).WillRepeatedly(Return(false));
  useDownstreamConfig(
      createConfig("tls_certificate_selector_dynamic", mock_tls_config_, serverContextFactory()));
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  EXPECT_TRUE(servesCertificate(selectDownstream("dynamic.lyft.com").selected_ctx, "server2"));
}

// A certificate provided before the configuration is ready is only built once it is ready, and is
// removed if the build fails.
TEST_F(DynamicModuleTlsCertificateSelectorTest, DeferredInvalidDynamicCertificateIsRemoved) {
  initializeDownstream("tls_certificate_selector_dynamic");
  EXPECT_CALL(mock_tls_config_, validationContextConfigured()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_tls_config_, certificateValidationContext()).WillRepeatedly(Return(nullptr));
  useDownstreamConfig(
      createConfig("tls_certificate_selector_dynamic", mock_tls_config_, serverContextFactory()));
  // The private key does not match the certificate.
  ASSERT_TRUE(setCertificatePem("dynamic.lyft.com", readCertificateFile("server2cert.pem"),
                                readCertificateFile("serverkey.pem")));
  auto callback = newCallback();
  EXPECT_CALL(*callback, onCertificateSelectionResult(_, false))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_FALSE(tls_context.has_value());
      });
  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", {}, std::move(callback));
  EXPECT_EQ(SelectionStatus::Pending, result.status);

  StrictMock<Ssl::MockCertificateValidationContextConfig> validation_context;
  EXPECT_CALL(mock_tls_config_, certificateValidationContext())
      .WillRepeatedly(Return(&validation_context));
  EXPECT_LOG_CONTAINS("warn", "dynamic module TLS certificate 'dynamic.lyft.com' is removed",
                      { EXPECT_TRUE(factory_->onConfigUpdate().ok()); });
}

// A rejected certificate answers the request, so the handshakes that wait for it fail and the next
// handshake requests it again.
TEST_F(DynamicModuleTlsCertificateSelectorTest, RejectedCertificateFailsWaitingHandshakes) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  auto callback = newCallback();
  EXPECT_CALL(*callback, onCertificateSelectionResult(_, false))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_FALSE(tls_context.has_value());
      });
  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", {}, std::move(callback));
  ASSERT_EQ(SelectionStatus::Pending, result.status);
  EXPECT_FALSE(setCertificatePem("dynamic.lyft.com", "not a certificate", "not a key"));

  const Ssl::SelectionResult retry = selectDownstream("dynamic.lyft.com", {}, newCallback());
  EXPECT_EQ(SelectionStatus::Pending, retry.status);
  EXPECT_EQ(2U, counterValue("dynamicmodulescustom.requested"));
}

// The requested hook can provide other certificates first, which grows the map that holds the
// requested one.
TEST_F(DynamicModuleTlsCertificateSelectorTest, RequestedHookProvidesOtherCertificatesFirst) {
  initializeDownstream("tls_certificate_selector_dynamic");
  useDownstreamConfig(createConfig("tls_certificate_selector_dynamic", *server_config_,
                                   serverContextFactory(), false, certificateAndKeyPem("server2")));
  auto callback = newCallback();
  EXPECT_CALL(*callback, onCertificateSelectionResult(_, false))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_TRUE(servesCertificate(tls_context.ptr(), "server2"));
      });
  const Ssl::SelectionResult result = selectDownstream("many.lyft.com", {}, std::move(callback));
  EXPECT_EQ(SelectionStatus::Pending, result.status);
  EXPECT_TRUE(servesCertificate(selectDownstream("dynamic.31.lyft.com").selected_ctx, "server2"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, ConfigUpdateRebuildsDynamicCertificates) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  auto callback = newCallback();
  // A requested name without a certificate is not rebuilt.
  const Ssl::SelectionResult waiting =
      selectDownstream("dynamic.example.com", {}, std::move(callback));
  EXPECT_EQ(SelectionStatus::Pending, waiting.status);
  const Ssl::SelectionResult before = selectDownstream("dynamic.lyft.com");

  EXPECT_TRUE(factory_->onConfigUpdate().ok());
  const Ssl::SelectionResult after = selectDownstream("dynamic.lyft.com");
  EXPECT_NE(before.selected_ctx, after.selected_ctx);
  EXPECT_TRUE(servesCertificate(after.selected_ctx, "server2"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, ConfigUpdateRemovesDynamicCertificatesThatFail) {
  initializeDownstream("tls_certificate_selector_dynamic");
  bool fail_build = false;
  useDownstreamConfig(createConfig(
      "tls_certificate_selector_dynamic", *server_config_,
      [this, &fail_build](Stats::Scope& scope,
                          Server::Configuration::ServerFactoryContext& server_context,
                          const Ssl::TlsCertificateConfig& certificate_config,
                          absl::Status& creation_status) -> AsyncContextConstSharedPtr {
        if (fail_build) {
          creation_status = absl::InvalidArgumentError("the build fails");
          return nullptr;
        }
        return std::make_shared<ServerAsyncContext>(scope, server_context, *server_config_,
                                                    certificate_config, creation_status);
      }));
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  EXPECT_EQ(SelectionStatus::Success, selectDownstream("dynamic.lyft.com").status);

  fail_build = true;
  EXPECT_LOG_CONTAINS("warn",
                      "dynamic module TLS certificate 'dynamic.lyft.com' is removed, since it "
                      "fails with the updated TLS context: the build fails",
                      { EXPECT_TRUE(factory_->onConfigUpdate().ok()); });
  // The removed certificate is requested again.
  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", {}, newCallback());
  EXPECT_EQ(SelectionStatus::Pending, result.status);
  EXPECT_EQ(1U, counterValue("dynamicmodulescustom.requested"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, FinishedHandshakesAreNotNotified) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  auto first_callback = newCallback();
  auto& first = *first_callback;
  const Ssl::SelectionResult first_result =
      selectDownstream("dynamic.lyft.com", {}, std::move(first_callback));
  // The second handshake finishes before the certificate arrives, and its handle is dropped when
  // the third handshake finds the list full.
  selectDownstream("dynamic.lyft.com", {}, newCallback());
  auto third_callback = newCallback();
  auto& third = *third_callback;
  const Ssl::SelectionResult third_result =
      selectDownstream("dynamic.lyft.com", {}, std::move(third_callback));

  EXPECT_CALL(first, onCertificateSelectionResult(_, false));
  EXPECT_CALL(third, onCertificateSelectionResult(_, false));
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
}

// The finished handshakes are dropped as more handshakes wait, so they do not accumulate, and they
// are neither resumed nor failed.
TEST_F(DynamicModuleTlsCertificateSelectorTest, FinishedHandshakesDoNotAccumulate) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(SelectionStatus::Pending,
              selectDownstream("dynamic.lyft.com", {}, newCallback()).status);
  }
  EXPECT_EQ(1U, DynamicModuleTlsCertificateSelectorConfigPeer::waitingHandleCount(
                    *config_, "dynamic.lyft.com"));
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));

  EXPECT_EQ(SelectionStatus::Pending,
            selectDownstream("dynamic.example.com", {}, newCallback()).status);
  removeCertificate("dynamic.example.com");
}

// The module provides the certificate after the worker misses it, but before the request of the
// worker reaches the main thread.
TEST_F(DynamicModuleTlsCertificateSelectorTest, CertificateProvidedBeforeRequestIsServed) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_))
      .WillOnce([&posted](Event::PostCb cb) { posted.push_back(std::move(cb)); });
  auto callback = newCallback();
  auto& waiting = *callback;
  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", {}, std::move(callback));
  EXPECT_EQ(SelectionStatus::Pending, result.status);
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));

  EXPECT_CALL(waiting, onCertificateSelectionResult(_, false))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_TRUE(servesCertificate(tls_context.ptr(), "server2"));
      });
  ASSERT_EQ(1U, posted.size());
  posted[0]();
  EXPECT_EQ(0U, counterValue("dynamicmodulescustom.requested"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, RequestIsDroppedWhenHandshakeOrConfigIsGone) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_))
      .WillRepeatedly([&posted](Event::PostCb cb) { posted.push_back(std::move(cb)); });

  // The handshake is gone before the request reaches the main thread.
  EXPECT_EQ(SelectionStatus::Pending,
            selectDownstream("dynamic.lyft.com", {}, newCallback()).status);
  ASSERT_EQ(1U, posted.size());
  posted[0]();
  EXPECT_EQ(0U, counterValue("dynamicmodulescustom.requested"));

  // The configuration is gone before the request reaches the main thread.
  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", {}, newCallback());
  ASSERT_EQ(2U, posted.size());
  selector_.reset();
  factory_.reset();
  config_.reset();
  posted[1]();
  EXPECT_EQ(0U, counterValue("dynamicmodulescustom.requested"));
}

// A served certificate outlives the configuration, and a waiting handshake is not resumed after
// the configuration is gone.
TEST_F(DynamicModuleTlsCertificateSelectorTest, DynamicCertificatesOutliveConfig) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  const Ssl::SelectionResult served = selectDownstream("dynamic.lyft.com");
  const Ssl::SelectionResult waiting = selectDownstream("dynamic.example.com", {}, newCallback());
  ASSERT_EQ(SelectionStatus::Pending, waiting.status);
  selector_.reset();
  factory_.reset();
  config_.reset();
  EXPECT_TRUE(servesCertificate(served.selected_ctx, "server2"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, DynamicCertificatesStopWithTlsContext) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  auto callback = newCallback();
  EXPECT_CALL(*callback, onCertificateSelectionResult(_, false))
      .WillOnce([](OptRef<const Ssl::TlsContext> tls_context, bool) {
        EXPECT_FALSE(tls_context.has_value());
      });
  const Ssl::SelectionResult result = selectDownstream("dynamic.lyft.com", {}, std::move(callback));
  ASSERT_EQ(SelectionStatus::Pending, result.status);
  factory_.reset();
  EXPECT_LOG_CONTAINS("debug",
                      "dynamic module set TLS certificate 'dynamic.lyft.com', but its TLS context "
                      "is removed",
                      { EXPECT_FALSE(setCertificate("dynamic.lyft.com", "server2")); });
}

// The module can still change certificates from its destroy hooks once threading shuts down.
TEST_F(DynamicModuleTlsCertificateSelectorTest, DynamicCertificatesAfterThreadingShutdown) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  factory_context_.server_context_.thread_local_.shutdownGlobalThreading();
  EXPECT_CALL(factory_context_.server_context_.thread_local_, runOnAllThreads(_)).Times(0);
  EXPECT_TRUE(setCertificate("dynamic.lyft.com", "server"));
  removeCertificate("dynamic.lyft.com");
  // The thread local caches keep their certificates after shutdown until the thread shuts down.
  factory_context_.server_context_.thread_local_.shutdownThread();
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, DynamicCertificatesRequireMainThread) {
  initializeDownstream("tls_certificate_selector_dynamic", downstreamTlsContext(), true);
  EXPECT_ENVOY_BUG(
      {
        std::thread thread([this] { EXPECT_FALSE(setCertificate("dynamic.lyft.com", "server2")); });
        thread.join();
      },
      "envoy_dynamic_module_callback_tls_certificate_selector_config_set_certificate must be "
      "called on the main thread");
  EXPECT_ENVOY_BUG(
      {
        std::thread thread([this] { removeCertificate("dynamic.lyft.com"); });
        thread.join();
      },
      "envoy_dynamic_module_callback_tls_certificate_selector_config_remove_certificate must be "
      "called on the main thread");
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, UpstreamSelectsCertificateByServerName) {
  UpstreamTlsContext tls_context;
  addCertificate(*tls_context.mutable_common_tls_context(), "server");
  addCertificate(*tls_context.mutable_common_tls_context(), "server2");
  initializeUpstream(tls_context, "tls_certificate_selector_by_server_name");

  const Ssl::SelectionResult result = selectUpstream("www.lyft2.com");
  EXPECT_EQ(SelectionStatus::Success, result.status);
  EXPECT_EQ(&client_context_->getTlsContexts()[1], result.selected_ctx);
  EXPECT_FALSE(result.staple);
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, UpstreamDefaultServesFirstCertificate) {
  UpstreamTlsContext tls_context;
  addCertificate(*tls_context.mutable_common_tls_context(), "server");
  addCertificate(*tls_context.mutable_common_tls_context(), "server2");
  initializeUpstream(tls_context, "tls_certificate_selector_dynamic");
  EXPECT_EQ(&client_context_->getTlsContexts()[0], selectUpstream("www.lyft2.com").selected_ctx);
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, UpstreamContextWithoutCertificateHasEmptyEntry) {
  initializeUpstream(UpstreamTlsContext(), "tls_certificate_selector_by_server_name");
  // Without a server name the module selects the last entry, which has no certificate.
  const Ssl::SelectionResult result = upstream_selector_->selectTlsContext(*ssl_, nullptr, nullptr);
  EXPECT_EQ(SelectionStatus::Success, result.status);
  EXPECT_EQ(&client_context_->getTlsContexts()[0], result.selected_ctx);
  EXPECT_EQ(nullptr, result.selected_ctx->cert_chain_);
  // The empty entry matches no server name.
  EXPECT_EQ(SelectionStatus::Failed, selectUpstream("www.lyft.com").status);
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, UpstreamContextWithoutCertificateHasOneEntry) {
  initializeUpstream(UpstreamTlsContext(), "tls_certificate_selector_out_of_range");
  EXPECT_LOG_CONTAINS(
      "debug", "dynamic module selected TLS certificate index 1, but the TLS context has 1",
      { EXPECT_EQ(SelectionStatus::Failed, selectUpstream("www.lyft.com").status); });
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, UpstreamServesDynamicCertificate) {
  initializeUpstreamWithDynamicCertificates();
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  const Ssl::SelectionResult result = selectUpstream("dynamic.lyft.com");
  EXPECT_EQ(SelectionStatus::Success, result.status);
  EXPECT_TRUE(servesCertificate(result.selected_ctx, "server2"));
  EXPECT_FALSE(result.staple);
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, UpstreamConfigUpdateRebuildsDynamicCertificates) {
  initializeUpstreamWithDynamicCertificates();
  ASSERT_TRUE(setCertificate("dynamic.lyft.com", "server2"));
  const Ssl::SelectionResult before = selectUpstream("dynamic.lyft.com");
  EXPECT_TRUE(upstream_factory_->onConfigUpdate().ok());
  const Ssl::SelectionResult after = selectUpstream("dynamic.lyft.com");
  EXPECT_NE(before.selected_ctx, after.selected_ctx);
  EXPECT_TRUE(servesCertificate(after.selected_ctx, "server2"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, SchedulerDeliversEventsToModule) {
  initializeDownstream("tls_certificate_selector_dynamic");
  auto* scheduler =
      envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_new(config_.get());
  envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_commit(scheduler, 3);
  envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_commit(scheduler, 4);
  EXPECT_EQ(7U, counterValue("dynamicmodulescustom.scheduled"));

  // A module without the scheduled hook ignores the events.
  config_ = createConfig("tls_certificate_selector_by_server_name", *server_config_, nullptr);
  auto* other_scheduler =
      envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_new(config_.get());
  envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_commit(other_scheduler,
                                                                                 1);

  // An event committed after the configuration is gone does nothing.
  selector_.reset();
  factory_.reset();
  config_.reset();
  envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_commit(scheduler, 5);
  EXPECT_EQ(7U, counterValue("dynamicmodulescustom.scheduled"));
  envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_delete(scheduler);
  envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_delete(other_scheduler);
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, ScheduledEventIsDroppedWhenConfigIsGone) {
  initializeDownstream("tls_certificate_selector_dynamic");
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_))
      .WillOnce([&posted](Event::PostCb cb) { posted.push_back(std::move(cb)); });
  auto* scheduler =
      envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_new(config_.get());
  envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_commit(scheduler, 1);
  selector_.reset();
  factory_.reset();
  config_.reset();
  ASSERT_EQ(1U, posted.size());
  posted[0]();
  envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_delete(scheduler);
  EXPECT_EQ(0U, counterValue("dynamicmodulescustom.scheduled"));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, MetricsAreDefinedOnlyDuringConfigCreation) {
  initializeDownstream("tls_certificate_selector_dynamic");
  size_t id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_tls_certificate_selector_config_define_counter(
                config_.get(), moduleBuffer("late"), nullptr, 0, &id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_tls_certificate_selector_config_define_gauge(
                config_.get(), moduleBuffer("late"), nullptr, 0, &id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_tls_certificate_selector_config_define_histogram(
                config_.get(), moduleBuffer("late"), nullptr, 0, &id));
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, MetricsAreRecorded) {
  initializeDownstream("tls_certificate_selector_by_server_name");
  config_->stat_creation_frozen_.store(false);
  void* config_ptr = config_.get();
  auto& server_store = factory_context_.server_context_.store_;
  std::array<envoy_dynamic_module_type_module_buffer, 1> label_names = {moduleBuffer("name")};
  std::array<envoy_dynamic_module_type_module_buffer, 1> label_values = {moduleBuffer("lyft")};

  size_t counter_id = 0;
  size_t counter_vec_id = 0;
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_define_counter(
                config_ptr, moduleBuffer("counter"), nullptr, 0, &counter_id));
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_define_counter(
                config_ptr, moduleBuffer("counter_vec"), label_names.data(), 1, &counter_vec_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
                config_ptr, counter_id, nullptr, 0, 2));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
                config_ptr, counter_vec_id, label_values.data(), 1, 3));
  EXPECT_EQ(2U, counterValue("dynamicmodulescustom.counter"));
  EXPECT_EQ(3U, counterValue("dynamicmodulescustom.counter_vec.name.lyft"));
  // The module metrics are not in the scope of the TLS context.
  EXPECT_EQ(nullptr,
            TestUtility::findCounter(factory_context_.store_, "dynamicmodulescustom.counter"));

  size_t gauge_id = 0;
  size_t gauge_vec_id = 0;
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_define_gauge(
                config_ptr, moduleBuffer("gauge"), nullptr, 0, &gauge_id));
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_define_gauge(
                config_ptr, moduleBuffer("gauge_vec"), label_names.data(), 1, &gauge_vec_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_set_gauge(
                config_ptr, gauge_id, nullptr, 0, 10));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_increment_gauge(
                config_ptr, gauge_id, nullptr, 0, 5));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_decrement_gauge(
                config_ptr, gauge_id, nullptr, 0, 3));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_set_gauge(
                config_ptr, gauge_vec_id, label_values.data(), 1, 7));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_increment_gauge(
                config_ptr, gauge_vec_id, label_values.data(), 1, 2));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_decrement_gauge(
                config_ptr, gauge_vec_id, label_values.data(), 1, 1));
  EXPECT_EQ(12U, TestUtility::findGauge(server_store, "dynamicmodulescustom.gauge")->value());
  EXPECT_EQ(
      8U,
      TestUtility::findGauge(server_store, "dynamicmodulescustom.gauge_vec.name.lyft")->value());

  size_t histogram_id = 0;
  size_t histogram_vec_id = 0;
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_define_histogram(
                config_ptr, moduleBuffer("histogram"), nullptr, 0, &histogram_id));
  ASSERT_EQ(
      envoy_dynamic_module_type_metrics_result_Success,
      envoy_dynamic_module_callback_tls_certificate_selector_config_define_histogram(
          config_ptr, moduleBuffer("histogram_vec"), label_names.data(), 1, &histogram_vec_id));
  EXPECT_CALL(server_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "dynamicmodulescustom.histogram"), 42));
  EXPECT_CALL(server_store, deliverHistogramToSinks(
                                testing::Property(&Stats::Metric::name,
                                                  "dynamicmodulescustom.histogram_vec.name.lyft"),
                                43));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_record_histogram_value(
                config_ptr, histogram_id, nullptr, 0, 42));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tls_certificate_selector_config_record_histogram_value(
                config_ptr, histogram_vec_id, label_values.data(), 1, 43));

  // An id records only to its own metric, with label values that match its definition. So a call
  // with the id of the other form or of another metric type changes no metric.
  std::array<envoy_dynamic_module_type_module_buffer, 2> extra_label_values = {
      moduleBuffer("lyft"), moduleBuffer("extra")};
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
                config_ptr, counter_vec_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
                config_ptr, counter_vec_id, extra_label_values.data(), 2, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
                config_ptr, counter_id, label_values.data(), 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
                config_ptr, gauge_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_tls_certificate_selector_config_set_gauge(
                config_ptr, gauge_vec_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_tls_certificate_selector_config_set_gauge(
                config_ptr, gauge_id, label_values.data(), 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_tls_certificate_selector_config_record_histogram_value(
                config_ptr, histogram_vec_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_tls_certificate_selector_config_record_histogram_value(
                config_ptr, histogram_id, label_values.data(), 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
                config_ptr, 42, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_tls_certificate_selector_config_set_gauge(
                config_ptr, 42, label_values.data(), 1, 1));
  EXPECT_EQ(2U, counterValue("dynamicmodulescustom.counter"));
  EXPECT_EQ(3U, counterValue("dynamicmodulescustom.counter_vec.name.lyft"));
  EXPECT_EQ(12U, TestUtility::findGauge(server_store, "dynamicmodulescustom.gauge")->value());
  EXPECT_EQ(
      8U,
      TestUtility::findGauge(server_store, "dynamicmodulescustom.gauge_vec.name.lyft")->value());
}

TEST_F(DynamicModuleTlsCertificateSelectorTest, SelectionContextReadsTheHandshake) {
  SSL_CLIENT_HELLO client_hello{};
  client_hello.ssl = ssl_.get();
  client_hello.extensions = AlpnExtension;
  client_hello.extensions_len = sizeof(AlpnExtension);
  TlsCertificateSelectionContext downstream_context{*ssl_, &client_hello};
  TlsCertificateSelectionContext upstream_context{*ssl_, nullptr};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};

  EXPECT_FALSE(envoy_dynamic_module_callback_tls_certificate_selector_get_requested_server_name(
      &downstream_context, &result));
  ASSERT_EQ(1, SSL_set_tlsext_host_name(ssl_.get(), "www.lyft.com"));
  ASSERT_TRUE(envoy_dynamic_module_callback_tls_certificate_selector_get_requested_server_name(
      &downstream_context, &result));
  EXPECT_EQ("www.lyft.com", absl::string_view(result.ptr, result.length));

  ASSERT_TRUE(envoy_dynamic_module_callback_tls_certificate_selector_get_client_hello_extension(
      &downstream_context, TLSEXT_TYPE_application_layer_protocol_negotiation, &result));
  EXPECT_EQ(absl::string_view("\x00\x03\x02h2", 5), absl::string_view(result.ptr, result.length));
  EXPECT_FALSE(envoy_dynamic_module_callback_tls_certificate_selector_get_client_hello_extension(
      &downstream_context, TLSEXT_TYPE_status_request, &result));
  EXPECT_FALSE(envoy_dynamic_module_callback_tls_certificate_selector_get_client_hello_extension(
      &upstream_context, TLSEXT_TYPE_application_layer_protocol_negotiation, &result));

  // A handshake that is not attached to a connection has no filter state.
  EXPECT_FALSE(envoy_dynamic_module_callback_tls_certificate_selector_get_filter_state(
      &downstream_context, moduleBuffer("tenant"), &result));

  StrictMock<Network::MockTransportSocketCallbacks> callbacks;
  EXPECT_CALL(callbacks, connection()).Times(2);
  callbacks.connection_.stream_info_.filter_state_->setData(
      "tenant", std::make_shared<Router::StringAccessorImpl>("lyft"));
  ASSERT_EQ(1, SSL_set_ex_data(ssl_.get(), ContextImpl::sslSocketIndex(), &callbacks));
  ASSERT_TRUE(envoy_dynamic_module_callback_tls_certificate_selector_get_filter_state(
      &upstream_context, moduleBuffer("tenant"), &result));
  EXPECT_EQ("lyft", absl::string_view(result.ptr, result.length));
  EXPECT_FALSE(envoy_dynamic_module_callback_tls_certificate_selector_get_filter_state(
      &upstream_context, moduleBuffer("other"), &result));
  ASSERT_EQ(1, SSL_set_ex_data(ssl_.get(), ContextImpl::sslSocketIndex(), nullptr));
}

class DynamicModuleTlsCertificateSelectorConfigFactoryTest
    : public DynamicModuleTlsCertificateSelectorTest {
protected:
  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
  createDownstreamFactory(const DynamicModuleTlsCertificateSelectorProto& proto_config,
                          const Ssl::ServerContextConfig& tls_config, bool for_quic = false) {
    return Config::Utility::getAndCheckFactoryByName<Ssl::TlsCertificateSelectorConfigFactory>(
               "envoy.tls.certificate_selectors.dynamic_modules")
        .createTlsCertificateSelectorFactory(proto_config, factory_context_, tls_config, for_quic);
  }

  absl::StatusOr<Ssl::UpstreamTlsCertificateSelectorFactoryPtr>
  createUpstreamFactory(const DynamicModuleTlsCertificateSelectorProto& proto_config) {
    return Config::Utility::getAndCheckFactoryByName<
               Ssl::UpstreamTlsCertificateSelectorConfigFactory>(
               "envoy.tls.certificate_selectors.dynamic_modules")
        .createUpstreamTlsCertificateSelectorFactory(proto_config, factory_context_,
                                                     client_tls_config_);
  }

  uint64_t failureCounter(absl::string_view leaf) {
    return Envoy::Extensions::DynamicModules::failureCounter(
        factory_context_.server_context_.scope(), leaf, "selector");
  }

  StrictMock<Ssl::MockServerContextConfig> server_tls_config_;
  StrictMock<Ssl::MockClientContextConfig> client_tls_config_;
};

TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest, EmptyConfigProto) {
  auto& downstream_factory =
      Config::Utility::getAndCheckFactoryByName<Ssl::TlsCertificateSelectorConfigFactory>(
          "envoy.tls.certificate_selectors.dynamic_modules");
  auto& upstream_factory =
      Config::Utility::getAndCheckFactoryByName<Ssl::UpstreamTlsCertificateSelectorConfigFactory>(
          "envoy.tls.certificate_selectors.dynamic_modules");
  EXPECT_NE(nullptr, dynamic_cast<DynamicModuleTlsCertificateSelectorProto*>(
                         downstream_factory.createEmptyConfigProto().get()));
  EXPECT_NE(nullptr, dynamic_cast<DynamicModuleTlsCertificateSelectorProto*>(
                         upstream_factory.createEmptyConfigProto().get()));
}

TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest, DownstreamFactoryCreatesSelectors) {
  initializeDownstream("tls_certificate_selector_by_server_name");
  auto factory =
      createDownstreamFactory(selectorProto("tls_certificate_selector_dynamic"), *server_config_);
  ASSERT_TRUE(factory.ok());
  EXPECT_TRUE((*factory)->onConfigUpdate().ok());

  // The selector serves the Default outcome with the default certificate selection.
  auto selector = (*factory)->create(*server_context_);
  ASSERT_EQ(1, SSL_set_tlsext_host_name(ssl_.get(), "www.lyft2.com"));
  SSL_CLIENT_HELLO client_hello{};
  client_hello.ssl = ssl_.get();
  EXPECT_EQ(&server_context_->getTlsContexts()[1],
            selector->selectTlsContext(client_hello, nullptr).selected_ctx);
}

// A TLS context can serve only dynamic certificates, which the module can provide while its
// configuration is created.
TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest, ServesOnlyDynamicCertificates) {
  DynamicModuleTlsCertificateSelectorProto proto_config =
      selectorProto("tls_certificate_selector_dynamic", certificateAndKeyPem("server2"));
  proto_config.set_allow_dynamic_certificates(true);
  DownstreamTlsContext tls_context;
  tls_context.set_disable_stateless_session_resumption(true);
  tls_context.set_disable_stateful_session_resumption(true);
  auto* selector_config =
      tls_context.mutable_common_tls_context()->mutable_custom_tls_certificate_selector();
  selector_config->set_name("envoy.tls.certificate_selectors.dynamic_modules");
  ASSERT_TRUE(selector_config->mutable_typed_config()->PackFrom(proto_config));
  auto server_config = ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  ASSERT_TRUE(server_config.ok()) << server_config.status();
  auto server_context = ServerContextImpl::create(*store_.rootScope(), **server_config,
                                                  factory_context_.serverFactoryContext(), nullptr);
  ASSERT_TRUE(server_context.ok()) << server_context.status();

  Ssl::TlsCertificateSelectorPtr selector =
      (*server_config)->tlsCertificateSelectorFactory().create(**server_context);
  EXPECT_TRUE(selector->providesCertificates());
  ASSERT_EQ(1, SSL_set_tlsext_host_name(ssl_.get(), "dynamic.prefetch.lyft.com"));
  SSL_CLIENT_HELLO client_hello{};
  client_hello.ssl = ssl_.get();
  const Ssl::SelectionResult result = selector->selectTlsContext(client_hello, nullptr);
  EXPECT_EQ(SelectionStatus::Success, result.status);
  EXPECT_TRUE(servesCertificate(result.selected_ctx, "server2"));
  EXPECT_EQ(0U, counterValue("dynamicmodulescustom.requested"));
}

TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest, DownstreamFactoryRejectsQuic) {
  EXPECT_THAT(
      createDownstreamFactory(selectorProto("tls_certificate_selector_by_server_name"),
                              server_tls_config_, true)
          .status(),
      HasStatusMessage("Dynamic module TLS certificate selector does not support QUIC listeners"));
}

TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest,
       DownstreamDynamicCertificatesRequireDisabledSessionResumption) {
  DynamicModuleTlsCertificateSelectorProto proto_config =
      selectorProto("tls_certificate_selector_dynamic");
  proto_config.set_allow_dynamic_certificates(true);
  const std::string error = "Dynamic certificates require both stateless and stateful session "
                            "resumption to be disabled";

  EXPECT_CALL(server_tls_config_, disableStatelessSessionResumption())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(server_tls_config_, disableStatefulSessionResumption()).WillRepeatedly(Return(true));
  EXPECT_THAT(createDownstreamFactory(proto_config, server_tls_config_).status(),
              HasStatusMessage(error));
  EXPECT_CALL(server_tls_config_, disableStatelessSessionResumption()).WillRepeatedly(Return(true));
  EXPECT_CALL(server_tls_config_, disableStatefulSessionResumption()).WillRepeatedly(Return(false));
  EXPECT_THAT(createDownstreamFactory(proto_config, server_tls_config_).status(),
              HasStatusMessage(error));
  EXPECT_CALL(server_tls_config_, disableStatefulSessionResumption()).WillRepeatedly(Return(true));
  EXPECT_TRUE(createDownstreamFactory(proto_config, server_tls_config_).ok());
}

TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest,
       UpstreamDynamicCertificatesRequireNoSessionKeys) {
  DynamicModuleTlsCertificateSelectorProto proto_config =
      selectorProto("tls_certificate_selector_dynamic");
  proto_config.set_allow_dynamic_certificates(true);
  EXPECT_CALL(client_tls_config_, maxSessionKeys()).WillRepeatedly(Return(1));
  EXPECT_THAT(createUpstreamFactory(proto_config).status(),
              HasStatusMessage("Dynamic certificates require max_session_keys to be 0"));
  EXPECT_CALL(client_tls_config_, maxSessionKeys()).WillRepeatedly(Return(0));
  auto factory = createUpstreamFactory(proto_config);
  ASSERT_TRUE(factory.ok());
  EXPECT_CALL(client_tls_config_, validationContextConfigured()).WillOnce(Return(false));
  EXPECT_TRUE((*factory)->onConfigUpdate().ok());
}

TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest, FactoriesPassDirectionToModule) {
  EXPECT_TRUE(createDownstreamFactory(
                  selectorProto("tls_certificate_selector_by_server_name", "downstream"),
                  server_tls_config_)
                  .ok());
  EXPECT_TRUE(
      createUpstreamFactory(selectorProto("tls_certificate_selector_by_server_name", "upstream"))
          .ok());
  EXPECT_EQ(0U, failureCounter(Envoy::Extensions::DynamicModules::ConfigInitErrorStat));

  EXPECT_THAT(
      createDownstreamFactory(selectorProto("tls_certificate_selector_by_server_name", "upstream"),
                              server_tls_config_)
          .status(),
      HasStatusMessage("Failed to initialize dynamic module TLS certificate selector config"));
  EXPECT_FALSE(
      createUpstreamFactory(selectorProto("tls_certificate_selector_by_server_name", "downstream"))
          .ok());
  EXPECT_EQ(2U, failureCounter(Envoy::Extensions::DynamicModules::ConfigInitErrorStat));
}

TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest, ModuleLoadFailureIsAnError) {
  EXPECT_FALSE(createDownstreamFactory(selectorProto("does_not_exist"), server_tls_config_).ok());
  EXPECT_EQ(1U, failureCounter(Envoy::Extensions::DynamicModules::ModuleLoadErrorStat));
}

TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest, MalformedSelectorConfigIsAnError) {
  DynamicModuleTlsCertificateSelectorProto proto_config =
      selectorProto("tls_certificate_selector_by_server_name");
  proto_config.mutable_selector_config()->set_type_url(
      "type.googleapis.com/google.protobuf.StringValue");
  proto_config.mutable_selector_config()->set_value("\xff");
  EXPECT_FALSE(createDownstreamFactory(proto_config, server_tls_config_).ok());
  EXPECT_EQ(1U, failureCounter(Envoy::Extensions::DynamicModules::ConfigInitErrorStat));
}

TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest, MetricsUseTheMetricsNamespace) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});
  initializeDownstream("tls_certificate_selector_by_server_name");
  DynamicModuleTlsCertificateSelectorProto proto_config =
      selectorProto("tls_certificate_selector_dynamic");
  proto_config.mutable_dynamic_module_config()->set_metrics_namespace("tls_selector");
  auto factory = createDownstreamFactory(proto_config, *server_config_);
  ASSERT_TRUE(factory.ok());
  EXPECT_TRUE(api_->customStatNamespaces().registered("tls_selector"));

  ASSERT_EQ(1, SSL_set_tlsext_host_name(ssl_.get(), "lyft.com"));
  SSL_CLIENT_HELLO client_hello{};
  client_hello.ssl = ssl_.get();
  (*factory)->create(*server_context_)->selectTlsContext(client_hello, nullptr);
  EXPECT_EQ(1U, counterValue("tls_selector.selected.outcome.configured"));
}

TEST_F(DynamicModuleTlsCertificateSelectorConfigFactoryTest, MissingSymbolIsAnError) {
  const auto config_status = [this](const std::string& module_name) {
    auto module =
        Envoy::Extensions::DynamicModules::newDynamicModuleByName(module_name, false, false);
    EXPECT_TRUE(module.ok());
    return newDynamicModuleTlsCertificateSelectorConfig(
               "selector", "", false, DefaultMetricsNamespace, std::move(module.value()),
               factory_context_, server_tls_config_, nullptr)
        .status();
  };
  // The no_op module implements the HTTP filter ABI, not the certificate selector ABI.
  EXPECT_THAT(
      config_status("no_op"),
      HasStatusMessage(HasSubstr("envoy_dynamic_module_on_tls_certificate_selector_config_new")));
  EXPECT_THAT(config_status("tls_certificate_selector_missing_config_destroy"),
              HasStatusMessage(
                  HasSubstr("envoy_dynamic_module_on_tls_certificate_selector_config_destroy")));
  EXPECT_THAT(config_status("tls_certificate_selector_missing_new"),
              HasStatusMessage(HasSubstr("envoy_dynamic_module_on_tls_certificate_selector_new")));
  EXPECT_THAT(
      config_status("tls_certificate_selector_missing_destroy"),
      HasStatusMessage(HasSubstr("envoy_dynamic_module_on_tls_certificate_selector_destroy")));
  EXPECT_THAT(
      config_status("tls_certificate_selector_missing_select"),
      HasStatusMessage(HasSubstr("envoy_dynamic_module_on_tls_certificate_selector_select")));
}

} // namespace
} // namespace DynamicModules
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
