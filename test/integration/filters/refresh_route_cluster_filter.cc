#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A test filter that clears the route cache on creation
class RefreshRouteClusterFilter : public Http::PassThroughFilter {
public:
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks.downstreamCallbacks()->clearRouteCache();
    Http::PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    initial_cluster_ = decoder_callbacks_->route()->routeEntry()->clusterName();
    has_initial_cluster_info_ = decoder_callbacks_->clusterInfo() != nullptr;

    headers.setCopy(Http::LowerCaseString("env"), "prod");
    decoder_callbacks_->downstreamCallbacks()->refreshRouteCluster();

    refreshed_cluster_ = decoder_callbacks_->route()->routeEntry()->clusterName();
    has_refreshed_cluster_info_ = decoder_callbacks_->clusterInfo() != nullptr;

    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override {
    headers.setCopy(Http::LowerCaseString("initial-cluster"), initial_cluster_);
    headers.setCopy(Http::LowerCaseString("refreshed-cluster"), refreshed_cluster_);
    headers.setCopy(Http::LowerCaseString("has-initial-cluster-info"),
                    fmt::format("{}", has_initial_cluster_info_));
    headers.setCopy(Http::LowerCaseString("has-refreshed-cluster-info"),
                    fmt::format("{}", has_refreshed_cluster_info_));

    return Http::FilterHeadersStatus::Continue;
  }

private:
  std::string initial_cluster_;
  bool has_initial_cluster_info_{};

  std::string refreshed_cluster_;
  bool has_refreshed_cluster_info_{};
};

class RefreshRouteClusterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  RefreshRouteClusterConfig() : EmptyHttpFilterConfig("refresh-route-cluster") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::RefreshRouteClusterFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<RefreshRouteClusterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
