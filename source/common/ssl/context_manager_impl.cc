#include "context_impl.h"
#include "context_manager_impl.h"

#include "common/common/assert.h"

namespace Ssl {

ContextManagerImpl::~ContextManagerImpl() { ASSERT(contexts_.empty()); }

void ContextManagerImpl::releaseContext(Context* context) {
  std::unique_lock<std::mutex> lock(contexts_lock_);
  ASSERT(std::find(contexts_.begin(), contexts_.end(), context) != contexts_.end());
  contexts_.remove(context);
}

ClientContextPtr ContextManagerImpl::createSslClientContext(Stats::Scope& scope,
                                                            ContextConfig& config) {

  ClientContextPtr context(new ClientContextImpl(*this, scope, config));
  std::unique_lock<std::mutex> lock(contexts_lock_);
  contexts_.emplace_back(context.get());
  return context;
}

ServerContextPtr ContextManagerImpl::createSslServerContext(Stats::Scope& scope,
                                                            ContextConfig& config) {
  ServerContextPtr context(new ServerContextImpl(*this, scope, config, runtime_));
  std::unique_lock<std::mutex> lock(contexts_lock_);
  contexts_.emplace_back(context.get());
  return context;
}

size_t ContextManagerImpl::daysUntilFirstCertExpires() {
  std::unique_lock<std::mutex> lock(contexts_lock_);
  size_t ret = std::numeric_limits<int>::max();
  for (Context* context : contexts_) {
    ret = std::min<size_t>(context->daysUntilFirstCertExpires(), ret);
  }
  return ret;
}

void ContextManagerImpl::iterateContexts(std::function<void(Context&)> callback) {
  std::unique_lock<std::mutex> lock(contexts_lock_);
  for (Context* context : contexts_) {
    callback(*context);
  }
}

} // Ssl
