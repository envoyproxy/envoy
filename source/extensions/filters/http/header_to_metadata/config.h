#pragma once

#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"
#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

/**
 * Config registration for the header-to-metadata filter. @see NamedHttpFilterConfigFactory.
 */
class HeaderToMetadataConfig
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::header_to_metadata::v3::Config> {
public:
  HeaderToMetadataConfig() : ExceptionFreeFactoryBase("envoy.filters.http.header_to_metadata") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::header_to_metadata::v3::Config& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::header_to_metadata::v3::Config& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  // Shared factory creation used by both the downstream (FactoryContext) and route/vhost-level
  // (ServerFactoryContext) paths. Stats are scoped to the given scope.
  static absl::StatusOr<Http::FilterFactoryCb> createFilterFactory(
      const envoy::extensions::filters::http::header_to_metadata::v3::Config& proto_config,
      Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope);

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::header_to_metadata::v3::Config& config,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
