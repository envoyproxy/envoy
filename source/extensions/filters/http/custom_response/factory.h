#pragma once

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

inline constexpr absl::string_view FilterName = "envoy.filters.http.custom_response";

class CustomResponseFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::custom_response::v3::CustomResponse>,
      public Logger::Loggable<Logger::Id::filter> {
public:
  CustomResponseFilterFactory() : FactoryBase(std::string(FilterName)) {}
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::custom_response::v3::CustomResponse& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
