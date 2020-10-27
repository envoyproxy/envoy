#pragma once

#include <functional>
#include <list>

#include "envoy/common/time.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/stats/scope.h"

#include "extensions/transport_sockets/tls/private_key/private_key_manager_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

/**
 * The SSL context manager has the following threading model:
 * Contexts can be allocated via any thread (through in practice they are only allocated on the main
 * thread). They can be released from any thread (and in practice are since cluster information can
 * be released from any thread). Context allocation/free is a very uncommon thing so we just do a
 * global lock to protect it all.
 */
class ContextManagerImpl final : public Envoy::Ssl::ContextManager {
public:
  ContextManagerImpl(TimeSource& time_source) : time_source_(time_source) {}
  ~ContextManagerImpl() override;

  // Ssl::ContextManager
  Ssl::ClientContextSharedPtr
  createSslClientContext(Stats::Scope& scope, const Envoy::Ssl::ClientContextConfig& config,
                         Envoy::Ssl::ClientContextSharedPtr old_context) override;
  Ssl::ServerContextSharedPtr
  createSslServerContext(Stats::Scope& scope, const Envoy::Ssl::ServerContextConfig& config,
                         const std::vector<std::string>& server_names,
                         Envoy::Ssl::ServerContextSharedPtr old_context) override;
  size_t daysUntilFirstCertExpires() const override;
  absl::optional<uint64_t> secondsUntilFirstOcspResponseExpires() const override;
  void iterateContexts(std::function<void(const Envoy::Ssl::Context&)> callback) override;
  Ssl::PrivateKeyMethodManager& privateKeyMethodManager() override {
    return private_key_method_manager_;
  };

private:
  void removeEmptyContexts();
  void removeOldContext(std::shared_ptr<Envoy::Ssl::Context> old_context);
  TimeSource& time_source_;
  std::list<std::weak_ptr<Envoy::Ssl::Context>> contexts_;
  PrivateKeyMethodManagerImpl private_key_method_manager_{};
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
