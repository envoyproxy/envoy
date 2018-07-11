#include "common/ssl/context_manager_impl.h"

#include <functional>
#include <shared_mutex>

#include "common/common/assert.h"
#include "common/ssl/context_impl.h"

namespace Envoy {
namespace Ssl {

ContextManagerImpl::~ContextManagerImpl() {
  removeEmptyContexts();
  ASSERT(contexts_.empty());
}

void ContextManagerImpl::removeEmptyContexts() {
  for (auto it = contexts_.begin(); it != contexts_.end();) {
    if (!it->lock()) {
      it = contexts_.erase(it);
    } else {
      ++it;
    }
  }
}

ClientContextSharedPtr
ContextManagerImpl::createSslClientContext(Stats::Scope& scope, const ClientContextConfig& config) {
  ClientContextSharedPtr context = std::make_shared<ClientContextImpl>(scope, config);
  std::unique_lock<std::shared_timed_mutex> lock(contexts_lock_);
  removeEmptyContexts();
  contexts_.emplace_back(context);
  return context;
}

ServerContextSharedPtr
ContextManagerImpl::createSslServerContext(Stats::Scope& scope, const ServerContextConfig& config,
                                           const std::vector<std::string>& server_names) {
  ServerContextSharedPtr context =
      std::make_shared<ServerContextImpl>(scope, config, server_names, runtime_);
  std::unique_lock<std::shared_timed_mutex> lock(contexts_lock_);
  removeEmptyContexts();
  contexts_.emplace_back(context);
  return context;
}

size_t ContextManagerImpl::daysUntilFirstCertExpires() const {
  std::shared_lock<std::shared_timed_mutex> lock(contexts_lock_);
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
  std::shared_lock<std::shared_timed_mutex> lock(contexts_lock_);
  for (const auto& ctx_weak_ptr : contexts_) {
    ContextSharedPtr context = ctx_weak_ptr.lock();
    if (context) {
      callback(*context);
    }
  }
}

} // namespace Ssl
} // namespace Envoy
