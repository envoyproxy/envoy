#include "source/extensions/filters/http/mcp_router/filter_config.h"

#include "source/extensions/filters/common/mcp/filter_state.h"

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
} // namespace

McpRouterConfig::McpRouterConfig(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
    Server::Configuration::FactoryContext& context)
    : factory_context_(context), session_identity_(parseSessionIdentity(proto_config)),
      metadata_namespace_(Filters::Common::Mcp::metadataNamespace()) {
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
