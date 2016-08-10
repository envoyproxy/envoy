#include "context_manager_impl.h"

namespace Ssl {

Ssl::ClientContext& ContextManagerImpl::createSslClientContext(const std::string& name,
                                                               Stats::Store& stats,
                                                               ContextConfig& config) {

  Ssl::ClientContext* context = new ClientContextImpl(name, stats, config);
  contexts_.emplace_back(context);
  return *context;
}

Ssl::ServerContext& ContextManagerImpl::createSslServerContext(const std::string& name,
                                                               Stats::Store& stats,
                                                               ContextConfig& config) {
  Ssl::ServerContext* context = new ServerContextImpl(name, stats, config, runtime_);
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
