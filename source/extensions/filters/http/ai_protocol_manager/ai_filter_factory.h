#pragma once

#include <functional>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Per-stream inputs for an AI filter; outlives the FilterManager that runs the filters.
struct AiFilterContext {
  StreamInfo::StreamInfo& stream_info;
  const Http::RequestHeaderMap& request_headers;
  // Route-declared request wire API; Unspecified when the route named none.
  ApiProtocol request_protocol;
};

// Creates one AiFilter per stream, or nullptr to skip the stream; built once at config load.
using AiFilterFactoryCb = std::function<AiFilterPtr(const AiFilterContext& context)>;
using AiFilterFactories = std::vector<AiFilterFactoryCb>;

// Extension point behind RequestHandling.filters (category "envoy.filters.ai").
class AiFilterConfigFactory : public Config::TypedFactory {
public:
  ~AiFilterConfigFactory() override = default;

  // `scope` is the AI Protocol Manager's stats scope.
  virtual absl::StatusOr<AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext& context,
                        Stats::Scope& scope) PURE;

  std::string category() const override { return "envoy.filters.ai"; }
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
