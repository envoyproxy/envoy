#include "source/extensions/filters/http/mcp_router/filter_config.h"

#include <utility>
#include <vector>

#include "source/extensions/filters/common/mcp/filter_state.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

namespace {
SessionIdentityConfig
parseSessionIdentity(const envoy::extensions::filters::http::mcp_router::v3::McpRouter& config) {
  SessionIdentityConfig result;

  if (!config.has_session_identity()) {
    return result;
  }

  const auto& session_identity = config.session_identity();
  const auto& identity_extractor = session_identity.identity();

  // Exactly one of header or dynamic_metadata must be set.
  if (identity_extractor.has_header()) {
    result.subject_source = HeaderSubjectSource{identity_extractor.header().name()};
  } else if (identity_extractor.has_dynamic_metadata()) {
    const auto& metadata_key = identity_extractor.dynamic_metadata().key();
    std::vector<std::string> path_keys;
    path_keys.reserve(metadata_key.path().size());
    for (const auto& segment : metadata_key.path()) {
      path_keys.push_back(segment.key());
    }
    result.subject_source = MetadataSubjectSource{metadata_key.key(), std::move(path_keys)};
  }

  if (session_identity.has_validation()) {
    switch (session_identity.validation().mode()) {
    case envoy::extensions::filters::http::mcp_router::v3::ValidationPolicy::ENFORCE:
      result.validation_mode = ValidationMode::Enforce;
      break;
    default:
      result.validation_mode = ValidationMode::Disabled;
      break;
    }
  }

  return result;
}

McpRouterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = absl::StrCat(prefix, "mcp_router.");
  return McpRouterStats{MCP_ROUTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

template <typename Config> std::vector<McpBackendConfig> parseBackends(const Config& config) {
  std::vector<McpBackendConfig> result;
  for (const auto& server : config.servers()) {
    McpBackendConfig backend;
    const auto& mcp_cluster = server.mcp_cluster();
    backend.name = server.name().empty() ? mcp_cluster.cluster() : server.name();
    backend.cluster_name = mcp_cluster.cluster();
    backend.path = mcp_cluster.path().empty() ? "/mcp" : mcp_cluster.path();
    backend.timeout =
        std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(mcp_cluster, timeout, 5000));
    backend.host_rewrite_literal = mcp_cluster.host_rewrite_literal();
    result.push_back(std::move(backend));
  }
  return result;
}
} // namespace

McpRouterConfigImpl::McpRouterConfigImpl(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope,
    Server::Configuration::FactoryContext& context)
    : backends_(parseBackends(proto_config)),
      default_backend_name_(backends_.size() == 1 ? backends_[0].name : ""),
      factory_context_(context), session_identity_(parseSessionIdentity(proto_config)),
      metadata_namespace_(Filters::Common::Mcp::metadataNamespace()),
      stats_(generateStats(stats_prefix, scope)) {}

const McpBackendConfig* McpRouterConfigImpl::findBackend(const std::string& name) const {
  for (const auto& backend : backends_) {
    if (backend.name == name) {
      return &backend;
    }
  }
  return nullptr;
}

McpRouterClusterConfigImpl::McpRouterClusterConfigImpl(
    const envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig& proto_config,
    McpRouterConfigSharedPtr base_config)
    : base_config_(std::move(base_config)), backends_(parseBackends(proto_config)),
      default_backend_name_(backends_.size() == 1 ? backends_[0].name : "") {}

const McpBackendConfig* McpRouterClusterConfigImpl::findBackend(const std::string& name) const {
  for (const auto& backend : backends_) {
    if (backend.name == name) {
      return &backend;
    }
  }
  return base_config_->findBackend(name);
}

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
