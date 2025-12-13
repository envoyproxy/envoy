#include "source/extensions/filters/http/mcp_router/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

McpRouterConfig::McpRouterConfig(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
    Server::Configuration::FactoryContext& context)
    : factory_context_(context) {
  for (const auto& server : proto_config.servers()) {
    McpBackendConfig backend;
    const auto& mcp_cluster = server.mcp_cluster();
    backend.name = server.name().empty() ? mcp_cluster.cluster() : server.name();
    backend.cluster_name = mcp_cluster.cluster();
    backend.path = mcp_cluster.path().empty() ? "/mcp" : mcp_cluster.path();
    backend.timeout =
        std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(mcp_cluster, timeout, 5000));
    backend.host_rewrite_literal = mcp_cluster.host_rewrite_literal();
    backends_.push_back(std::move(backend));
  }

  if (backends_.size() == 1) {
    default_backend_name_ = backends_[0].name;
  }
}

const McpBackendConfig* McpRouterConfig::findBackend(const std::string& name) const {
  for (const auto& backend : backends_) {
    if (backend.name == name) {
      return &backend;
    }
  }
  return nullptr;
}

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
