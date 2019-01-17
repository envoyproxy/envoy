#pragma once

#include <functional>
#include <list>

#include "envoy/common/time.h"
#include "envoy/stats/scope.h"
#include "envoy/tls/context_manager.h"

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
class ContextManagerImpl final : public Envoy::Tls::ContextManager {
public:
  ContextManagerImpl(TimeSource& time_source) : time_source_(time_source) {}
  ~ContextManagerImpl();

  // Tls::ContextManager
  Envoy::Tls::ClientContextSharedPtr
  createSslClientContext(Stats::Scope& scope,
                         const Envoy::Tls::ClientContextConfig& config) override;
  Envoy::Tls::ServerContextSharedPtr
  createSslServerContext(Stats::Scope& scope, const Envoy::Tls::ServerContextConfig& config,
                         const std::vector<std::string>& server_names) override;
  size_t daysUntilFirstCertExpires() const override;
  void iterateContexts(std::function<void(const Envoy::Tls::Context&)> callback) override;

private:
  void removeEmptyContexts();
  TimeSource& time_source_;
  std::list<std::weak_ptr<Envoy::Tls::Context>> contexts_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
