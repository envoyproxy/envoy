#include "context_manager_impl.h"

namespace Ssl {

Ssl::ClientContext& ContextManagerImpl::createSslClientContext(Stats::Scope& scope,
                                                               ContextConfig& config) {

  Ssl::ClientContext* context = new ClientContextImpl(scope, config);
  contexts_.emplace_back(context);
  return *context;
}

Ssl::ServerContext& ContextManagerImpl::createSslServerContext(Stats::Scope& scope,
                                                               ContextConfig& config) {
  Ssl::ServerContext* context = new ServerContextImpl(scope, config, runtime_);
  contexts_.emplace_back(context);
  return *context;
}

size_t ContextManagerImpl::daysUntilFirstCertExpires() {
  size_t ret = std::numeric_limits<int>::max();
  for (std::unique_ptr<Context>& context : contexts_) {
    ret = std::min<size_t>(context->daysUntilFirstCertExpires(), ret);
  }
  return ret;
}

std::vector<std::reference_wrapper<Context>> ContextManagerImpl::getContexts() {
  std::vector<std::reference_wrapper<Context>> return_contexts;
  for (std::unique_ptr<Context>& context : contexts_) {
    return_contexts.push_back(*context);
  }

  return return_contexts;
}

} // Ssl
