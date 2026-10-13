#pragma once

#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

/**
 * Config registration for the router filter. @see NamedHttpFilterConfigFactory.
 */
class IpTaggingFilterFactory : public Common::UnifiedFactoryBase<
                                   envoy::extensions::filters::http::ip_tagging::v3::IPTagging> {
public:
  IpTaggingFilterFactory() : UnifiedFactoryBase("envoy.filters.http.ip_tagging") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
