#pragma once

#include "envoy/extensions/filters/http/ai_session/v3/ai_session.pb.h"
#include "envoy/extensions/filters/http/ai_session/v3/ai_session.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

/**
 * NamedHttpFilterConfigFactory for the AI session filter.
 *
 * Registers "envoy.filters.http.ai_session" with Envoy's HTTP filter
 * registry so the filter can be added to any HCM filter chain via config:
 *
 *   http_filters:
 *     - name: envoy.filters.http.ai_session
 *       typed_config:
 *         "@type": type.googleapis.com/envoy.extensions.filters.http.ai_session.v3.AiSession
 *         ai_filters:
 *           - name: envoy.ai_filters.mcp_auth
 *             typed_config:
 *               "@type": type.googleapis.com/envoy.extensions.filters.http.ai_session.v3.McpAuthConfig
 *           - name: envoy.ai_filters.mcp_init
 *             typed_config:
 *               "@type": type.googleapis.com/envoy.extensions.filters.http.ai_session.v3.McpInitConfig
 *           - name: envoy.ai_filters.mcp_context
 *             typed_config:
 *               "@type": type.googleapis.com/envoy.extensions.filters.http.ai_session.v3.McpContextConfig
 *
 * Object lifetimes (mirrors how HCM owns its filter chain):
 *
 *   AiSessionFilterFactory  (singleton, created at config load)
 *     └─ AiSessionFilterConfig  (shared_ptr, one per filter config entry)
 *          └─ ThreadLocal::Slot  (one AiSessionManager per worker thread)
 *               └─ AiSessionManager  (per-worker, owns the session map)
 *
 *   Per HTTP request (created by the FilterFactoryCb):
 *     └─ JsonRpcConnectionManager  (Http::PassThroughDecoderFilter)
 *          └─ borrows AiSessionManager& from TLS slot
 */
class AiSessionFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::ai_session::v3::AiSession> {
public:
  AiSessionFilterFactory() : FactoryBase("envoy.filters.http.ai_session") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ai_session::v3::AiSession& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
