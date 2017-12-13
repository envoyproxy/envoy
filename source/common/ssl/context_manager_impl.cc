#include "common/ssl/context_manager_impl.h"

#include <functional>
#include <shared_mutex>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/ssl/context_impl.h"

namespace Envoy {
namespace Ssl {

ContextManagerImpl::~ContextManagerImpl() { ASSERT(contexts_.empty()); }

void ContextManagerImpl::releaseClientContext(ClientContext* context) {
  std::unique_lock<std::shared_timed_mutex> lock(contexts_lock_);

  // context may not be found, in the case that a subclass of Context throws
  // in it's constructor. In that case the context did not get added, but
  // the destructor of Context will run and call releaseContext().
  contexts_.remove(context);
}

void ContextManagerImpl::releaseServerContext(ServerContext* context,
                                              const std::string& listener_name,
                                              const std::vector<std::string>& server_names) {
  std::unique_lock<std::shared_timed_mutex> lock(contexts_lock_);

  // Remove mappings.
  auto& listener_map_exact = map_exact_[listener_name];
  if (server_names.empty()) {
    const auto ctx = listener_map_exact.find(EMPTY_STRING);
    if (ctx != listener_map_exact.end() && ctx->second == context) {
      listener_map_exact.erase(ctx);
    }
  } else {
    auto& listener_map_wildcard = map_wildcard_[listener_name];
    for (const auto& name : server_names) {
      if (isWildcardServerName(name)) {
        const auto ctx = listener_map_wildcard.find(name);
        if (ctx != listener_map_wildcard.end() && ctx->second == context) {
          listener_map_wildcard.erase(ctx);
        }
      } else {
        const auto ctx = listener_map_exact.find(name);
        if (ctx != listener_map_exact.end() && ctx->second == context) {
          listener_map_exact.erase(ctx);
        }
      }
    }
  }

  // context may not be found, in the case that a subclass of Context throws
  // in it's constructor. In that case the context did not get added, but
  // the destructor of Context will run and call releaseContext().
  contexts_.remove(context);
}

ClientContextPtr ContextManagerImpl::createSslClientContext(Stats::Scope& scope,
                                                            const ClientContextConfig& config) {
  ClientContextPtr context(new ClientContextImpl(*this, scope, config));
  std::unique_lock<std::shared_timed_mutex> lock(contexts_lock_);
  contexts_.emplace_back(context.get());
  return context;
}

bool ContextManagerImpl::isWildcardServerName(const std::string& name) {
  return name.size() > 2 && name[0] == '*' && name[1] == '.';
}

ServerContextPtr ContextManagerImpl::createSslServerContext(
    const std::string& listener_name, const std::vector<std::string>& server_names,
    Stats::Scope& scope, const ServerContextConfig& config, bool skip_context_update) {
  ServerContextPtr context(new ServerContextImpl(*this, listener_name, server_names, scope, config,
                                                 skip_context_update, runtime_));
  std::unique_lock<std::shared_timed_mutex> lock(contexts_lock_);
  contexts_.emplace_back(context.get());

  // Save mappings.
  if (server_names.empty()) {
    map_exact_[listener_name][EMPTY_STRING] = context.get();
  } else {
    for (const auto& name : server_names) {
      if (isWildcardServerName(name)) {
        map_wildcard_[listener_name][name] = context.get();
      } else {
        map_exact_[listener_name][name] = context.get();
      }
    }
  }

  return context;
}

ServerContext* ContextManagerImpl::findSslServerContext(const std::string& listener_name,
                                                        const std::string& server_name) const {
  // Find Ssl::ServerContext to use. The algorithm for "www.example.com" is as follows:
  // 1. Try exact match on domain, i.e. "www.example.com"
  // 2. Try exact match on wildcard, i.e. "*.example.com"
  // 3. Try "no SNI" match, i.e. ""
  // 4. Return no context and reject connection.

  // TODO(PiotrSikora): make this lockless.
  std::shared_lock<std::shared_timed_mutex> lock(contexts_lock_);

  // TODO(PiotrSikora): refactor and combine code with RouteMatcher::findVirtualHost().
  const auto listener_map_exact = map_exact_.find(listener_name);
  if (listener_map_exact != map_exact_.end()) {
    const auto ctx = listener_map_exact->second.find(server_name);
    if (ctx != listener_map_exact->second.end()) {
      return ctx->second;
    }
  }

  // Try to construct and match wildcard domain.
  const size_t pos = server_name.find('.');
  if (pos > 0 && pos < server_name.size() - 1) {
    const std::string wildcard = '*' + server_name.substr(pos);
    const auto listener_map_wildcard = map_wildcard_.find(listener_name);
    if (listener_map_wildcard != map_wildcard_.end()) {
      const auto ctx = listener_map_wildcard->second.find(wildcard);
      if (ctx != listener_map_wildcard->second.end()) {
        return ctx->second;
      }
    }
  }

  if (listener_map_exact != map_exact_.end()) {
    const auto ctx = listener_map_exact->second.find(EMPTY_STRING);
    if (ctx != listener_map_exact->second.end()) {
      return ctx->second;
    }
  }

  return nullptr;
}

size_t ContextManagerImpl::daysUntilFirstCertExpires() const {
  std::shared_lock<std::shared_timed_mutex> lock(contexts_lock_);
  size_t ret = std::numeric_limits<int>::max();
  for (Context* context : contexts_) {
    ret = std::min<size_t>(context->daysUntilFirstCertExpires(), ret);
  }
  return ret;
}

void ContextManagerImpl::iterateContexts(std::function<void(const Context&)> callback) {
  std::shared_lock<std::shared_timed_mutex> lock(contexts_lock_);
  for (Context* context : contexts_) {
    callback(*context);
  }
}

} // namespace Ssl
} // namespace Envoy
