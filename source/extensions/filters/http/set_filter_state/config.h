#pragma once

#include "envoy/extensions/filters/http/set_filter_state/v3/set_filter_state.pb.h"
#include "envoy/extensions/filters/http/set_filter_state/v3/set_filter_state.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/common/set_filter_state/filter_config.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetFilterState {

class SetFilterState : public Http::PassThroughDecoderFilter,
                       public Logger::Loggable<Logger::Id::filter> {
public:
  explicit SetFilterState(const Filters::Common::SetFilterState::ConfigSharedPtr config);

  // StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

private:
  const Filters::Common::SetFilterState::ConfigSharedPtr config_;
};

/**
 * Config registration. @see NamedHttpFilterConfigFactory.
 */
class SetFilterStateConfig
    : public Common::FactoryBase<envoy::extensions::filters::http::set_filter_state::v3::Config> {
public:
  SetFilterStateConfig() : FactoryBase("envoy.filters.http.set_filter_state") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::set_filter_state::v3::Config& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContextTyped(
      const envoy::extensions::filters::http::set_filter_state::v3::Config& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& server_context) override;
};

} // namespace SetFilterState
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
