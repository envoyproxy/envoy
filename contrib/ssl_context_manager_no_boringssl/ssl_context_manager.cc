#include <cstddef>

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"
#include "envoy/ssl/context_manager.h"

namespace Envoy {
namespace Contrib {

/**
 * A stub that provides a SSL context manager capable of reporting on
 * certificates' data in case there's no TLS implementation built
 * into Envoy.
 */
class SslContextManagerNoTlsStub final : public Envoy::Ssl::ContextManager {
  Ssl::ClientContextSharedPtr
  createSslClientContext(Stats::Scope& /* scope */,
                         const Envoy::Ssl::ClientContextConfig& /* config */) override {
    throwException();
  }

  Ssl::ServerContextSharedPtr createSslServerContext(
      Stats::Scope& /* scope */, const Envoy::Ssl::ServerContextConfig& /* config */,
      const std::vector<std::string>& /* server_names */, Ssl::ContextAdditionalInitFunc) override {
    throwException();
  }

  absl::optional<uint32_t> daysUntilFirstCertExpires() const override {
    return absl::make_optional(std::numeric_limits<uint32_t>::max());
  }
  absl::optional<uint64_t> secondsUntilFirstOcspResponseExpires() const override {
    return absl::nullopt;
  }

  void iterateContexts(std::function<void(const Envoy::Ssl::Context&)> /* callback */) override{};

  Ssl::PrivateKeyMethodManager& privateKeyMethodManager() override { throwException(); }

  void removeContext(const Envoy::Ssl::ContextSharedPtr& old_context) override {
    if (old_context) {
      throwEnvoyExceptionOrPanic("SSL is not supported in this configuration");
    }
  }

private:
  [[noreturn]] void throwException() {
    throwEnvoyExceptionOrPanic("SSL is not supported in this configuration");
  }
};

class SslContextManagerFactoryNoTls : public Ssl::ContextManagerFactory {
  Ssl::ContextManagerPtr createContextManager(TimeSource&) override {
    return std::make_unique<SslContextManagerNoTlsStub>();
  }
};

static Envoy::Registry::RegisterInternalFactory<SslContextManagerFactoryNoTls,
                                                Ssl::ContextManagerFactory>
    ssl_manager_registered;

} // namespace Contrib
} // namespace Envoy
