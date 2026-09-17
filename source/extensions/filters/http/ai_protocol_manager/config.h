#pragma once

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

class AiProtocolManagerFilterConfigFactory
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager,
          envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute> {
public:
  AiProtocolManagerFilterConfigFactory()
      : UnifiedFactoryBase("envoy.filters.http.ai_protocol_manager") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager&
          proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute&
          proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

using UpstreamAiProtocolManagerFilterConfigFactory = AiProtocolManagerFilterConfigFactory;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
