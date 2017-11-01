#include "common/ssl/context_manager_impl.h"

#include <functional>
#include <mutex>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/ssl/context_impl.h"

namespace Envoy {
namespace Ssl {

ContextManagerImpl::~ContextManagerImpl() { ASSERT(contexts_.empty()); }

void ContextManagerImpl::releaseClientContext(ClientContext* context) {
  std::unique_lock<std::mutex> lock(contexts_lock_);

  // context may not be found, in the case that a subclass of Context throws
  // in it's constructor.  In that case the context did not get added, but
  // the destructor of Context will run and call releaseContext().
  contexts_.remove(context);
}

void ContextManagerImpl::releaseServerContext(ServerContext* context,
                                              const std::string& listener_name,
                                              const std::vector<std::string>& server_names) {
  std::unique_lock<std::mutex> lock(contexts_lock_);

  // Remove mappings.
  if (server_names.empty()) {
    if (map_exact_[listener_name][EMPTY_STRING] == context) {
      map_exact_[listener_name][EMPTY_STRING] = nullptr;
    }
  } else {
    for (const auto& name : server_names) {
      if (name.size() > 2 && name[0] == '*' && name[1] == '.') {
        if (map_wildcard_[listener_name][name] == context) {
          map_wildcard_[listener_name][name] = nullptr;
        }
      } else {
        if (map_exact_[listener_name][name] == context) {
          map_exact_[listener_name][name] = nullptr;
        }
      }
    }
  }

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

ServerContextPtr
ContextManagerImpl::createSslServerContext(const std::string& listener_name,
                                           const std::vector<std::string>& server_names,
                                           Stats::Scope& scope, ServerContextConfig& config) {
  ServerContextPtr context(
      new ServerContextImpl(*this, listener_name, server_names, scope, config, runtime_));
  std::unique_lock<std::mutex> lock(contexts_lock_);
  contexts_.emplace_back(context.get());

  // Save mappings.
  if (server_names.empty()) {
    map_exact_[listener_name][EMPTY_STRING] = context.get();
  } else {
    for (const auto& name : server_names) {
      if (name.size() > 2 && name[0] == '*' && name[1] == '.') {
        map_wildcard_[listener_name][name] = context.get();
      } else {
        map_exact_[listener_name][name] = context.get();
      }
    }
  }

  return context;
}

ServerContext* ContextManagerImpl::findSslServerContext(const std::string& listener_name,
                                                        const std::string& server_name) {
  std::unique_lock<std::mutex> lock(contexts_lock_);
  if (map_exact_[listener_name][server_name] != nullptr) {
    return map_exact_[listener_name][server_name];
  }

  // Try to construct and match wildcard domain.
  if (server_name.size() >= 5) {
    size_t pos = server_name.find('.');
    if (pos > 0) {
      size_t rpos = server_name.rfind('.');
      if (rpos > pos + 1 && rpos != server_name.size() - 1) {
        std::string wildcard = '*' + server_name.substr(pos);
        if (map_wildcard_[listener_name][wildcard] != nullptr) {
          return map_wildcard_[listener_name][wildcard];
        }
      }
    }
  }

  return map_exact_[listener_name][EMPTY_STRING];
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
