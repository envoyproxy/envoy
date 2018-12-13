#pragma once

#include <functional>
#include <list>

#include "envoy/common/time.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Ssl {

/**
 * The SSL context manager has the following threading model:
 * Contexts can be allocated via any thread (through in practice they are only allocated on the main
 * thread). They can be released from any thread (and in practice are since cluster information can
 * be released from any thread). Context allocation/free is a very uncommon thing so we just do a
 * global lock to protect it all.
 */
class ContextManagerImpl final : public ContextManager {
public:
  ContextManagerImpl(TimeSource& time_source) : time_source_(time_source) {}
  ~ContextManagerImpl();

  // Ssl::ContextManager
  Ssl::ClientContextSharedPtr createSslClientContext(Stats::Scope& scope,
                                                     const ClientContextConfig& config) override;
  Ssl::ServerContextSharedPtr
  createSslServerContext(Stats::Scope& scope, const ServerContextConfig& config,
                         const std::vector<std::string>& server_names) override;
  size_t daysUntilFirstCertExpires() const override;
  void iterateContexts(std::function<void(const Context&)> callback) override;

private:
  void removeEmptyContexts();
  TimeSource& time_source_;
  std::list<std::weak_ptr<Context>> contexts_;
};

} // namespace Ssl
} // namespace Envoy
