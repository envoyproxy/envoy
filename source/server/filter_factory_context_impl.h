#pragma once

#include "envoy/server/factory_context.h"


namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Context for downstream network and HTTP filters.
 */
class FilterFactoryContextImpl  : public FilterFactoryContext {
public:
  FilterFactoryContextImpl(ServerFactoryContext& server_context,
                       OptRef<DownstreamFactoryContext> downstream_context)
      : server_context_(server_context), downstream_context_(downstream_context) {}

  ServerFactoryContext& getServerFactoryContext() override { return server_context_; }
  OptRef<DownstreamFactoryContext> getDownstreamFactoryContext() override { return downstream_context_; }

private:
  ServerFactoryContext& server_context_;
  OptRef<DownstreamFactoryContext> downstream_context_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy

