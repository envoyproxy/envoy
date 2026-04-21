#pragma once

#include "envoy/server/filter_config.h"

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
 *         "@type": type.googleapis.com/google.protobuf.Empty
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
    : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return "envoy.filters.http.ai_session"; }
};

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
