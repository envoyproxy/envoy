#pragma once

#include "context_config_impl.h"

#include "envoy/ssl/context_manager.h"

#include "common/ssl/context_impl.h"

namespace Ssl {

class ContextManagerImpl final : public ContextManager {
public:
  ContextManagerImpl(Runtime::Loader& runtime) : runtime_(runtime) {}

  // Ssl::ContextManager
  Ssl::ClientContext& createSslClientContext(Stats::Scope& scope, ContextConfig& config) override;

  Ssl::ServerContext& createSslServerContext(Stats::Scope& scope, ContextConfig& config) override;

  size_t daysUntilFirstCertExpires() override;

  std::vector<std::reference_wrapper<Context>> getContexts() override;

private:
  Runtime::Loader& runtime_;
  std::vector<std::unique_ptr<Context>> contexts_;
};

} // Ssl
