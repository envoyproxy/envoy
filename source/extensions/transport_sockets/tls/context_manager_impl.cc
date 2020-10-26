#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include <algorithm>
#include <functional>
#include <limits>

#include "envoy/stats/scope.h"

#include "common/common/assert.h"

#include "extensions/transport_sockets/tls/context_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

ContextManagerImpl::~ContextManagerImpl() {
  removeEmptyContexts();
  KNOWN_ISSUE_ASSERT(contexts_.empty(), "https://github.com/envoyproxy/envoy/issues/10030");
}

void ContextManagerImpl::removeEmptyContexts() {
  contexts_.remove_if([](const std::weak_ptr<Envoy::Ssl::Context>& n) { return n.expired(); });
}

void ContextManagerImpl::removeOldContext(std::shared_ptr<Envoy::Ssl::Context> old_context) {
  if (old_context) {
    contexts_.remove_if([old_context](const std::weak_ptr<Envoy::Ssl::Context>& n) {
      std::shared_ptr<Envoy::Ssl::Context> sp = n.lock();
      if (sp) {
        return old_context == sp;
      }
      return false;
    });
  }
}

Envoy::Ssl::ClientContextSharedPtr
ContextManagerImpl::createSslClientContext(Stats::Scope& scope,
                                           const Envoy::Ssl::ClientContextConfig& config,
                                           Envoy::Ssl::ClientContextSharedPtr old_context) {
  if (!config.isReady()) {
    return nullptr;
  }

  Envoy::Ssl::ClientContextSharedPtr context =
      std::make_shared<ClientContextImpl>(scope, config, time_source_);
  removeOldContext(old_context);
  removeEmptyContexts();
  contexts_.emplace_back(context);
  return context;
}

Envoy::Ssl::ServerContextSharedPtr ContextManagerImpl::createSslServerContext(
    Stats::Scope& scope, const Envoy::Ssl::ServerContextConfig& config,
    const std::vector<std::string>& server_names, Envoy::Ssl::ServerContextSharedPtr old_context) {
  if (!config.isReady()) {
    return nullptr;
  }

  Envoy::Ssl::ServerContextSharedPtr context =
      std::make_shared<ServerContextImpl>(scope, config, server_names, time_source_);
  removeOldContext(old_context);
  removeEmptyContexts();
  contexts_.emplace_back(context);
  return context;
}

size_t ContextManagerImpl::daysUntilFirstCertExpires() const {
  size_t ret = std::numeric_limits<int>::max();
  for (const auto& ctx_weak_ptr : contexts_) {
    Envoy::Ssl::ContextSharedPtr context = ctx_weak_ptr.lock();
    if (context) {
      ret = std::min<size_t>(context->daysUntilFirstCertExpires(), ret);
    }
  }
  return ret;
}

absl::optional<uint64_t> ContextManagerImpl::secondsUntilFirstOcspResponseExpires() const {
  absl::optional<uint64_t> ret;
  for (const auto& ctx_weak_ptr : contexts_) {
    Envoy::Ssl::ContextSharedPtr context = ctx_weak_ptr.lock();
    if (context) {
      auto next_expiration = context->secondsUntilFirstOcspResponseExpires();
      if (next_expiration) {
        ret = std::min<uint64_t>(next_expiration.value(),
                                 ret.value_or(std::numeric_limits<uint64_t>::max()));
      }
    }
  }
  return ret;
}

void ContextManagerImpl::iterateContexts(std::function<void(const Envoy::Ssl::Context&)> callback) {
  for (const auto& ctx_weak_ptr : contexts_) {
    Envoy::Ssl::ContextSharedPtr context = ctx_weak_ptr.lock();
    if (context) {
      callback(*context);
    }
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
