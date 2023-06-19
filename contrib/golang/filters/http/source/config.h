#pragma once

#include "envoy/server/lifecycle_notifier.h"

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/golang/v3alpha/golang.pb.h"
#include "contrib/envoy/extensions/filters/http/golang/v3alpha/golang.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

constexpr char CanonicalName[] = "envoy.filters.http.golang";

/**
 * Config registration for the golang extentions  filter. @see
 * NamedHttpFilterConfigFactory.
 */
class GolangFilterConfig : public Common::FactoryBase<
                               envoy::extensions::filters::http::golang::v3alpha::Config,
                               envoy::extensions::filters::http::golang::v3alpha::ConfigsPerRoute> {
public:
  GolangFilterConfig() : FactoryBase(CanonicalName) {}

private:
  Server::ServerLifecycleNotifier::HandlePtr handler_{};

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::golang::v3alpha::Config& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::FactoryContext& factory_context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::golang::v3alpha::ConfigsPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
