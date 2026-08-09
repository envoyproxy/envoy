#pragma once

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Dual-registered factory: downstream (listener) and upstream (cluster) HTTP
// filter. The upstream role serves deployments where handling must live on
// the cluster, e.g. a dynamic-forward-proxy egress cluster. Both roles
// install the full stream filter.
class AiProtocolManagerFilterConfigFactory
    : public Common::DualFactoryBase<
          envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager> {
public:
  AiProtocolManagerFilterConfigFactory()
      : DualFactoryBase("envoy.filters.http.ai_protocol_manager") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager&
          proto_config,
      const std::string& stats_prefix, DualInfo info,
      Server::Configuration::ServerFactoryContext& context) override;
};

using UpstreamAiProtocolManagerFilterConfigFactory = AiProtocolManagerFilterConfigFactory;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
