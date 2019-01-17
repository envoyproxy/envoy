#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include <functional>

#include "envoy/stats/scope.h"

#include "common/common/assert.h"

#include "extensions/transport_sockets/tls/context_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

ContextManagerImpl::~ContextManagerImpl() {
  removeEmptyContexts();
  ASSERT(contexts_.empty());
}

void ContextManagerImpl::removeEmptyContexts() {
  contexts_.remove_if([](const std::weak_ptr<Envoy::Tls::Context>& n) { return n.expired(); });
}

Envoy::Tls::ClientContextSharedPtr
ContextManagerImpl::createSslClientContext(Stats::Scope& scope,
                                           const Envoy::Tls::ClientContextConfig& config) {
  if (!config.isReady()) {
    return nullptr;
  }

  Envoy::Tls::ClientContextSharedPtr context =
      std::make_shared<ClientContextImpl>(scope, config, time_source_);
  removeEmptyContexts();
  contexts_.emplace_back(context);
  return context;
}

Envoy::Tls::ServerContextSharedPtr
ContextManagerImpl::createSslServerContext(Stats::Scope& scope,
                                           const Envoy::Tls::ServerContextConfig& config,
                                           const std::vector<std::string>& server_names) {
  if (!config.isReady()) {
    return nullptr;
  }

  Envoy::Tls::ServerContextSharedPtr context =
      std::make_shared<ServerContextImpl>(scope, config, server_names, time_source_);
  removeEmptyContexts();
  contexts_.emplace_back(context);
  return context;
}

size_t ContextManagerImpl::daysUntilFirstCertExpires() const {
  size_t ret = std::numeric_limits<int>::max();
  for (const auto& ctx_weak_ptr : contexts_) {
    Envoy::Tls::ContextSharedPtr context = ctx_weak_ptr.lock();
    if (context) {
      ret = std::min<size_t>(context->daysUntilFirstCertExpires(), ret);
    }
  }
  return ret;
}

void ContextManagerImpl::iterateContexts(std::function<void(const Envoy::Tls::Context&)> callback) {
  for (const auto& ctx_weak_ptr : contexts_) {
    Envoy::Tls::ContextSharedPtr context = ctx_weak_ptr.lock();
    if (context) {
      callback(*context);
    }
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
