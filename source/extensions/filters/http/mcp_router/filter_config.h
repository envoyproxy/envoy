#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

/**
 * Configuration for a single MCP backend server.
 */
struct McpBackendConfig {
  std::string name;
  std::string cluster_name;
  std::string path;
  std::chrono::milliseconds timeout{5000};
  std::string host_rewrite_literal;
};

/**
 * Configuration for the MCP router filter, containing backend server definitions.
 */
class McpRouterConfig {
public:
  McpRouterConfig(const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
                  Server::Configuration::FactoryContext& context);

  const std::vector<McpBackendConfig>& backends() const { return backends_; }
  bool isMultiplexing() const { return backends_.size() > 1; }
  const std::string& defaultBackendName() const { return default_backend_name_; }
  Server::Configuration::FactoryContext& factoryContext() const { return factory_context_; }
  const McpBackendConfig* findBackend(const std::string& name) const;

private:
  std::vector<McpBackendConfig> backends_;
  std::string default_backend_name_;
  Server::Configuration::FactoryContext& factory_context_;
};

using McpRouterConfigSharedPtr = std::shared_ptr<McpRouterConfig>;

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
