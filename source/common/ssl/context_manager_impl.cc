#include "common/ssl/context_manager_impl.h"

#include <functional>
#include <mutex>

#include "common/common/assert.h"
#include "common/ssl/context_impl.h"

namespace Envoy {
namespace Ssl {

ContextManagerImpl::~ContextManagerImpl() { ASSERT(contexts_.empty()); }

void ContextManagerImpl::releaseContext(Context* context) {
  std::unique_lock<std::mutex> lock(contexts_lock_);

  // context may not be found, in the case that a subclass of Context throws
  // in it's constructor.  In that case the context did not get added, but
  // the destructor of Context will run and call releaseContext().
  contexts_.remove(context);
}

ClientContextPtr ContextManagerImpl::createSslClientContext(Stats::Scope& scope,
                                                            ClientContextConfig& config) {

  ClientContextPtr context(new ClientContextImpl(*this, scope, config));
  std::unique_lock<std::mutex> lock(contexts_lock_);
  contexts_.emplace_back(context.get());
  return context;
}

ServerContextPtr ContextManagerImpl::createSslServerContext(Stats::Scope& scope,
                                                            ServerContextConfig& config) {
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

} // namespace Ssl
} // namespace Envoy
