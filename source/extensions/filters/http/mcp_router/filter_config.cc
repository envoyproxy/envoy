#include "source/extensions/filters/http/mcp_router/filter_config.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

namespace {
SubjectSource
parseSubjectValidation(const envoy::extensions::filters::http::mcp_router::v3::McpRouter& config) {
  if (!config.has_subject_validation()) {
    return absl::monostate{};
  }

  const auto& validation = config.subject_validation();
  switch (validation.subject_source_case()) {
  case envoy::extensions::filters::http::mcp_router::v3::SubjectValidation::kMetadata: {
    return MetadataSubjectSource{validation.metadata().filter(),
                                 absl::StrSplit(validation.metadata().path(), '.')};
  }
  case envoy::extensions::filters::http::mcp_router::v3::SubjectValidation::kHeader: {
    return HeaderSubjectSource{validation.header()};
  }
  default:
    return absl::monostate{};
  }
}
} // namespace

McpRouterConfig::McpRouterConfig(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
    Server::Configuration::FactoryContext& context)
    : factory_context_(context), subject_source_(parseSubjectValidation(proto_config)) {
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
