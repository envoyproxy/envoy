#include "source/common/http/filter_chain_helper.h"

#include <memory>
#include <string>

#include "envoy/registry/registry.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Http {

// Allows graceful handling of missing configuration for ECDS.
class MissingConfigFilter : public Http::PassThroughDecoderFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::NoFilterConfigFound);
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, EMPTY_STRING, nullptr,
                                       absl::nullopt, EMPTY_STRING);
    return Http::FilterHeadersStatus::StopIteration;
  }
};

static Http::FilterFactoryCb MissingConfigFilterFactory =
    [](Http::FilterChainFactoryCallbacks& cb) {
      cb.addStreamDecoderFilter(std::make_shared<MissingConfigFilter>());
    };

void FilterChainUtility::createFilterChainForFactories(
    Http::FilterChainManager& manager,
    const FilterChainUtility::FilterFactoriesList& filter_factories) {
  bool added_missing_config_filter = false;
  for (const auto& filter_config_provider : filter_factories) {
    auto config = filter_config_provider->config();
    if (config.has_value()) {
      Filter::NamedHttpFilterFactoryCb& factory_cb = config.value().get();
      manager.applyFilterFactoryCb({filter_config_provider->name(), factory_cb.name},
                                   factory_cb.factory_cb);
      continue;
    }

    // If a filter config is missing after warming, inject a local reply with status 500.
    if (!added_missing_config_filter) {
      ENVOY_LOG(trace, "Missing filter config for a provider {}", filter_config_provider->name());
      manager.applyFilterFactoryCb({}, MissingConfigFilterFactory);
      added_missing_config_filter = true;
    } else {
      ENVOY_LOG(trace, "Provider {} missing a filter config", filter_config_provider->name());
    }
  }
}

void FilterChainUtility::throwError(std::string message) { throw EnvoyException(message); }

SINGLETON_MANAGER_REGISTRATION(downstream_filter_config_provider_manager);
SINGLETON_MANAGER_REGISTRATION(upstream_filter_config_provider_manager);

std::shared_ptr<UpstreamFilterConfigProviderManager>
FilterChainUtility::createSingletonUpstreamFilterConfigProviderManager(
    Server::Configuration::ServerFactoryContext& context) {
  std::shared_ptr<UpstreamFilterConfigProviderManager> upstream_filter_config_provider_manager =
      context.singletonManager().getTyped<Http::UpstreamFilterConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(upstream_filter_config_provider_manager),
          [] { return std::make_shared<Filter::UpstreamHttpFilterConfigProviderManagerImpl>(); });
  return upstream_filter_config_provider_manager;
}

std::shared_ptr<Http::DownstreamFilterConfigProviderManager>
FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(
    Server::Configuration::ServerFactoryContext& context) {
  std::shared_ptr<Http::DownstreamFilterConfigProviderManager>
      downstream_filter_config_provider_manager =
          context.singletonManager().getTyped<Http::DownstreamFilterConfigProviderManager>(
              SINGLETON_MANAGER_REGISTERED_NAME(downstream_filter_config_provider_manager),
              [] { return std::make_shared<Filter::HttpFilterConfigProviderManagerImpl>(); });
  return downstream_filter_config_provider_manager;
}

} // namespace Http
} // namespace Envoy
