#include "envoy/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/v3/config.pb.h"

#include "source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/config.h"
#include "source/server/generic_factory_context.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace DynamicModulesCertSelector {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

class DynamicModuleCertSelectorTest : public testing::Test {
protected:
  DynamicModuleCertSelectorTest() {
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();

    // Default to "session resumption disabled" which is required when provides_certificates=true.
    ON_CALL(tls_config_, disableStatelessSessionResumption()).WillByDefault(Return(true));
    ON_CALL(tls_config_, disableStatefulSessionResumption()).WillByDefault(Return(true));
  }

  absl::StatusOr<DynamicModuleCertSelectorConfigSharedPtr>
  createConfig(const std::string& module_name, const std::string& selector_name = "test",
               const std::string& selector_config = "") {
    auto module =
        Envoy::Extensions::DynamicModules::newDynamicModuleByName(module_name, false, false);
    if (!module.ok()) {
      return module.status();
    }
    return newDynamicModuleCertSelectorConfig(selector_name, selector_config,
                                              std::move(module.value()));
  }

  envoy::extensions::transport_sockets::tls::cert_selectors::dynamic_modules::v3::Config
  makeProtoConfig(const std::string& module_name, bool provides_certificates = false) {
    envoy::extensions::transport_sockets::tls::cert_selectors::dynamic_modules::v3::Config cfg;
    cfg.mutable_dynamic_module_config()->set_name(module_name);
    cfg.set_selector_name("test");
    cfg.set_provides_certificates(provides_certificates);
    return cfg;
  }

  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
  createFactory(const envoy::extensions::transport_sockets::tls::cert_selectors::dynamic_modules::
                    v3::Config& cfg,
                bool for_quic = false) {
    Server::GenericFactoryContextImpl generic_ctx(factory_context_,
                                                  factory_context_.messageValidationVisitor());
    DynamicModuleCertSelectorConfigFactory factory;
    return factory.createTlsCertificateSelectorFactory(cfg, generic_ctx, tls_config_, for_quic);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  NiceMock<Ssl::MockServerContextConfig> tls_config_;
};

// =============================================================================
// Config creation tests.
// =============================================================================

TEST_F(DynamicModuleCertSelectorTest, ConfigNewSuccess) {
  auto config_or_error = createConfig("cert_selector_no_op");
  ASSERT_TRUE(config_or_error.ok()) << config_or_error.status().message();
  EXPECT_NE(config_or_error.value()->in_module_config_, nullptr);
  EXPECT_NE(config_or_error.value()->on_select_, nullptr);
}

TEST_F(DynamicModuleCertSelectorTest, ConfigNewReturnsNull) {
  auto config_or_error = createConfig("cert_selector_config_new_fail");
  ASSERT_FALSE(config_or_error.ok());
  EXPECT_THAT(config_or_error.status().message(),
              testing::HasSubstr("Failed to initialize dynamic module cert selector config"));
}

TEST_F(DynamicModuleCertSelectorTest, ConfigModuleNotFound) {
  auto module = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      "cert_selector_does_not_exist", false, false);
  EXPECT_FALSE(module.ok());
}

TEST_F(DynamicModuleCertSelectorTest, ConfigMissingSymbols) {
  // The "no_op" module has no cert selector ABI symbols at all.
  auto config_or_error = createConfig("no_op");
  ASSERT_FALSE(config_or_error.ok());
}

// =============================================================================
// Factory tests.
// =============================================================================

TEST_F(DynamicModuleCertSelectorTest, FactoryRejectsQuic) {
  auto cfg = makeProtoConfig("cert_selector_no_op");
  auto factory_or_error = createFactory(cfg, /*for_quic=*/true);
  ASSERT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(),
              testing::HasSubstr("does not support QUIC listeners"));
}

TEST_F(DynamicModuleCertSelectorTest, FactoryRejectsSessionResumptionWhenProvidingCerts) {
  // Re-arm the mock to report resumption enabled.
  ON_CALL(tls_config_, disableStatelessSessionResumption()).WillByDefault(Return(false));
  auto cfg = makeProtoConfig("cert_selector_no_op", /*provides_certificates=*/true);
  auto factory_or_error = createFactory(cfg);
  ASSERT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(),
              testing::HasSubstr("session resumption to be disabled"));
}

TEST_F(DynamicModuleCertSelectorTest, FactoryPermitsSessionResumptionWhenNotProvidingCerts) {
  // Resumption enabled is OK when the module only picks from pre-provisioned contexts.
  ON_CALL(tls_config_, disableStatelessSessionResumption()).WillByDefault(Return(false));
  ON_CALL(tls_config_, disableStatefulSessionResumption()).WillByDefault(Return(false));
  auto cfg = makeProtoConfig("cert_selector_no_op", /*provides_certificates=*/false);
  auto factory_or_error = createFactory(cfg);
  ASSERT_TRUE(factory_or_error.ok()) << factory_or_error.status().message();
  EXPECT_NE(factory_or_error.value(), nullptr);
}

TEST_F(DynamicModuleCertSelectorTest, FactoryModuleLoadFailure) {
  auto cfg = makeProtoConfig("cert_selector_does_not_exist");
  auto factory_or_error = createFactory(cfg);
  EXPECT_FALSE(factory_or_error.ok());
}

TEST_F(DynamicModuleCertSelectorTest, FactoryConfigNewNull) {
  auto cfg = makeProtoConfig("cert_selector_config_new_fail");
  auto factory_or_error = createFactory(cfg);
  ASSERT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(),
              testing::HasSubstr("Failed to initialize dynamic module cert selector config"));
}

TEST_F(DynamicModuleCertSelectorTest, FactoryOnConfigUpdateSucceedsWithoutOptionalHook) {
  // cert_selector_no_op does not implement the optional on_cert_selector_config_updated hook.
  auto cfg = makeProtoConfig("cert_selector_no_op");
  auto factory_or_error = createFactory(cfg);
  ASSERT_TRUE(factory_or_error.ok()) << factory_or_error.status().message();
  EXPECT_TRUE(factory_or_error.value()->onConfigUpdate().ok());
}

// =============================================================================
// Per-worker selector construction.
// =============================================================================

// Stub TlsCertificateSelectorContext exposing a fixed (empty) tls_contexts vector,
// used to drive factory->create() without standing up a full ServerContextImpl.
class StubSelectorContext : public Ssl::TlsCertificateSelectorContext {
public:
  const std::vector<Ssl::TlsContext>& getTlsContexts() const override { return tls_contexts_; }

private:
  std::vector<Ssl::TlsContext> tls_contexts_;
};

TEST_F(DynamicModuleCertSelectorTest, FactoryCreateInstantiatesPerWorkerSelector) {
  // cert_selector_context_index implements the full lifecycle (config_new, new, destroy,
  // config_destroy) and returns SuccessWithContextIndex from select. Constructing a selector
  // exercises on_cert_selector_new and its destructor exercises on_cert_selector_destroy.
  auto cfg = makeProtoConfig("cert_selector_context_index");
  auto factory_or_error = createFactory(cfg);
  ASSERT_TRUE(factory_or_error.ok()) << factory_or_error.status().message();

  StubSelectorContext selector_ctx;
  Ssl::TlsCertificateSelectorPtr selector = factory_or_error.value()->create(selector_ctx);
  EXPECT_NE(selector, nullptr);
  EXPECT_FALSE(selector->providesCertificates());
}

} // namespace
} // namespace DynamicModulesCertSelector
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
