#pragma once

#include "envoy/extensions/filters/http/tap/v3/tap.pb.h"
#include "envoy/extensions/filters/http/tap/v3/tap.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

/**
 * Config registration for the tap filter.
 */
class TapFilterFactory
    : public Common::UnifiedFactoryBase<envoy::extensions::filters::http::tap::v3::Tap> {
public:
  TapFilterFactory() : UnifiedFactoryBase("envoy.filters.http.tap") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::tap::v3::Tap& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

  // Shared factory creation used by the listener/cluster and route/vhost-level paths. The
  // FilterConfig stats are scoped to the given scope.
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactory(const envoy::extensions::filters::http::tap::v3::Tap& proto_config,
                      const std::string& stats_prefix,
                      Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
                      ProtobufMessage::ValidationVisitor& validation_visitor);
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
