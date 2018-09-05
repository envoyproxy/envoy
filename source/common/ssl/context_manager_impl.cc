#include "common/ssl/context_manager_impl.h"

#include <functional>
#include <shared_mutex>

#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/ssl/context_impl.h"

namespace Envoy {
namespace Ssl {

ContextManagerImpl::~ContextManagerImpl() {
  removeEmptyContexts();
  ASSERT(contexts_.empty());
}

void ContextManagerImpl::removeEmptyContexts() {
  contexts_.remove_if([](const std::weak_ptr<Context>& n) { return n.expired(); });
}

ClientContextSharedPtr
ContextManagerImpl::createSslClientContext(Stats::Scope& scope, const ClientContextConfig& config) {
  if (!config.isReady()) {
    return nullptr;
  }

  ClientContextSharedPtr context = std::make_shared<ClientContextImpl>(scope, config);
  removeEmptyContexts();
  contexts_.emplace_back(context);
  return context;
}

ServerContextSharedPtr
ContextManagerImpl::createSslServerContext(Stats::Scope& scope, const ServerContextConfig& config,
                                           const std::vector<std::string>& server_names) {
  if (!config.isReady()) {
    return nullptr;
  }

  ServerContextSharedPtr context =
      std::make_shared<ServerContextImpl>(scope, config, server_names, runtime_);
  removeEmptyContexts();
  contexts_.emplace_back(context);
  return context;
}

size_t ContextManagerImpl::daysUntilFirstCertExpires() const {
  size_t ret = std::numeric_limits<int>::max();
  for (const auto& ctx_weak_ptr : contexts_) {
    ContextSharedPtr context = ctx_weak_ptr.lock();
    if (context) {
      ret = std::min<size_t>(context->daysUntilFirstCertExpires(), ret);
    }
  }
  return ret;
}

void ContextManagerImpl::iterateContexts(std::function<void(const Context&)> callback) {
  for (const auto& ctx_weak_ptr : contexts_) {
    ContextSharedPtr context = ctx_weak_ptr.lock();
    if (context) {
      callback(*context);
    }
  }
}

} // namespace Ssl
} // namespace Envoy
