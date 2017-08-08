#pragma once

#include <functional>
#include <list>
#include <mutex>

#include "envoy/runtime/runtime.h"
#include "envoy/ssl/context_manager.h"

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
  ContextManagerImpl(Runtime::Loader& runtime) : runtime_(runtime) {}
  ~ContextManagerImpl();

  /**
   * Allocated contexts are owned by the caller. However, we need to be able to iterate them for
   * admin purposes. When a caller frees a context it will tell us to release it also from the list
   * of contexts.
   */
  void releaseContext(Context* context);

  // Ssl::ContextManager
  Ssl::ClientContextPtr createSslClientContext(Stats::Scope& scope,
                                               ClientContextConfig& config) override;
  Ssl::ServerContextPtr createSslServerContext(Stats::Scope& scope,
                                               ServerContextConfig& config) override;
  size_t daysUntilFirstCertExpires() override;
  void iterateContexts(std::function<void(Context&)> callback) override;

private:
  Runtime::Loader& runtime_;
  std::list<Context*> contexts_;
  std::mutex contexts_lock_;
};

} // namespace Ssl
} // namespace Envoy
