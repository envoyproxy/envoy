#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "absl/types/variant.h"

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

// Subject extraction from request header.
struct HeaderSubjectSource {
  std::string header_name;
};

// Subject extraction from dynamic metadata using MetadataKey.
struct MetadataSubjectSource {
  std::string filter;
  std::vector<std::string> path_keys;
};

using SubjectSource = absl::variant<absl::monostate, MetadataSubjectSource, HeaderSubjectSource>;

// Validation policy modes.
enum class ValidationMode {
  Disabled = 0,
  Enforce = 1,
};

// Session identity configuration.
struct SessionIdentityConfig {
  SubjectSource subject_source;
  ValidationMode validation_mode{ValidationMode::Disabled};
};

/**
 * All MCP router filter stats. @see stats_macros.h
 */
// clang-format off
#define MCP_ROUTER_STATS(COUNTER)                                                                  \
  COUNTER(rq_total)                                                                                \
  COUNTER(rq_fanout)                                                                               \
  COUNTER(rq_direct_response)                                                                      \
  COUNTER(rq_body_rewrite)                                                                         \
  COUNTER(rq_invalid)                                                                              \
  COUNTER(rq_unknown_backend)                                                                      \
  COUNTER(rq_backend_failure)                                                                      \
  COUNTER(rq_fanout_failure)                                                                       \
  COUNTER(rq_session_invalid)                                                                      \
  COUNTER(rq_auth_failure)
// clang-format on

/**
 * Struct definition for MCP router filter stats. @see stats_macros.h
 */
struct McpRouterStats {
  MCP_ROUTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the MCP router filter, containing backend server definitions.
 */
class McpRouterConfig {
public:
  McpRouterConfig(const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
                  const std::string& stats_prefix, Stats::Scope& scope,
                  Server::Configuration::FactoryContext& context);

  const std::vector<McpBackendConfig>& backends() const { return backends_; }
  bool isMultiplexing() const { return backends_.size() > 1; }
  const std::string& defaultBackendName() const { return default_backend_name_; }
  Server::Configuration::FactoryContext& factoryContext() const { return factory_context_; }
  const McpBackendConfig* findBackend(const std::string& name) const;

  bool hasSessionIdentity() const {
    return !absl::holds_alternative<absl::monostate>(session_identity_.subject_source);
  }
  const SubjectSource& subjectSource() const { return session_identity_.subject_source; }
  ValidationMode validationMode() const { return session_identity_.validation_mode; }
  bool shouldEnforceValidation() const {
    return session_identity_.validation_mode == ValidationMode::Enforce;
  }
  const std::string& metadataNamespace() const { return metadata_namespace_; }

  McpRouterStats& stats() { return stats_; }

private:
  std::vector<McpBackendConfig> backends_;
  std::string default_backend_name_;
  Server::Configuration::FactoryContext& factory_context_;
  SessionIdentityConfig session_identity_;
  std::string metadata_namespace_;
  McpRouterStats stats_;
};

using McpRouterConfigSharedPtr = std::shared_ptr<McpRouterConfig>;

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
