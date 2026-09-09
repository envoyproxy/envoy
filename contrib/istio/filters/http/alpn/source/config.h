#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/alpn/v3/alpn.pb.h"
#include "contrib/envoy/extensions/filters/http/alpn/v3/alpn.pb.validate.h"

namespace Envoy {
namespace Http {
namespace Alpn {

/**
 * Config registration for the alpn filter.
 */
class AlpnConfigFactory : public Extensions::HttpFilters::Common::UnifiedFactoryBase<
                              istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig> {
public:
  AlpnConfigFactory() : UnifiedFactoryBase("istio.alpn") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

  Http::FilterFactoryCb createFilterFactory(
      const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig& config_pb,
      Upstream::ClusterManager& cluster_manager);
};

} // namespace Alpn
} // namespace Http
} // namespace Envoy
