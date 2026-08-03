#pragma once

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

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
