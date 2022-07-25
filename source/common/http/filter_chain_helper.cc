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

void FilterChainHelper::createFilterChainForFactories(
    Http::FilterChainManager& manager,
    const FilterChainHelper::FilterFactoriesList& filter_factories) {
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

void FilterChainHelper::processFilters(const FiltersList& filters, const std::string& prefix,
                                       const std::string& filter_chain_type,
                                       FilterFactoriesList& filter_factories) {
  DependencyManager dependency_manager;
  for (int i = 0; i < filters.size(); i++) {
    processFilter(filters[i], i, prefix, filter_chain_type, i == filters.size() - 1,
                  filter_factories, dependency_manager);
  }
  // TODO(auni53): Validate encode dependencies too.
  auto status = dependency_manager.validDecodeDependencies();
  if (!status.ok()) {
    throw EnvoyException(std::string(status.message()));
  }
}

void FilterChainHelper::processFilter(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
        proto_config,
    int i, const std::string& prefix, const std::string& filter_chain_type,
    bool last_filter_in_current_config, FilterFactoriesList& filter_factories,
    DependencyManager& dependency_manager) {
  ENVOY_LOG(debug, "    {} filter #{}", prefix, i);
  if (proto_config.config_type_case() ==
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter::ConfigTypeCase::
          kConfigDiscovery) {
    processDynamicFilterConfig(proto_config.name(), proto_config.config_discovery(),
                               filter_factories, filter_chain_type, last_filter_in_current_config);
    return;
  }

  // Now see if there is a factory that will accept the config.
  auto* factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
          proto_config, proto_config.is_optional());
  // null pointer returned only when the filter is optional, then skip all the processes.
  if (factory == nullptr) {
    ENVOY_LOG(warn, "Didn't find a registered factory for the optional http filter {}",
              proto_config.name());
    return;
  }
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      proto_config, factory_context_.messageValidationVisitor(), *factory);
  Http::FilterFactoryCb callback =
      factory->createFilterFactoryFromProto(*message, stats_prefix_, factory_context_);
  dependency_manager.registerFilter(factory->name(), *factory->dependencies());
  bool is_terminal = factory->isTerminalFilterByProto(*message, factory_context_);
  Config::Utility::validateTerminalFilters(proto_config.name(), factory->name(), filter_chain_type,
                                           is_terminal, last_filter_in_current_config);
  auto filter_config_provider = filter_config_provider_manager_.createStaticFilterConfigProvider(
      {factory->name(), callback}, proto_config.name());
  ENVOY_LOG(debug, "      name: {}", filter_config_provider->name());
  ENVOY_LOG(debug, "    config: {}",
            MessageUtil::getJsonStringFromMessageOrError(
                static_cast<const Protobuf::Message&>(proto_config.typed_config())));
  filter_factories.push_back(std::move(filter_config_provider));
}

void FilterChainHelper::processDynamicFilterConfig(
    const std::string& name, const envoy::config::core::v3::ExtensionConfigSource& config_discovery,
    FilterFactoriesList& filter_factories, const std::string& filter_chain_type,
    bool last_filter_in_current_config) {
  ENVOY_LOG(debug, "      dynamic filter name: {}", name);
  if (config_discovery.apply_default_config_without_warming() &&
      !config_discovery.has_default_config()) {
    throw EnvoyException(fmt::format(
        "Error: filter config {} applied without warming but has no default config.", name));
  }
  for (const auto& type_url : config_discovery.type_urls()) {
    auto factory_type_url = TypeUtil::typeUrlToDescriptorFullName(type_url);
    auto* factory = Registry::FactoryRegistry<
        Server::Configuration::NamedHttpFilterConfigFactory>::getFactoryByType(factory_type_url);
    if (factory == nullptr) {
      throw EnvoyException(
          fmt::format("Error: no factory found for a required type URL {}.", factory_type_url));
    }
  }

  auto filter_config_provider = filter_config_provider_manager_.createDynamicFilterConfigProvider(
      config_discovery, name, factory_context_, stats_prefix_, last_filter_in_current_config,
      filter_chain_type, nullptr);
  filter_factories.push_back(std::move(filter_config_provider));
}

SINGLETON_MANAGER_REGISTRATION(filter_config_provider_manager);

std::shared_ptr<FilterConfigProviderManager>
FilterChainHelper::createSingletonFilterConfigProviderManager(
    Server::Configuration::ServerFactoryContext& context) {
  std::shared_ptr<FilterConfigProviderManager> filter_config_provider_manager =
      context.singletonManager().getTyped<FilterConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(filter_config_provider_manager),
          [] { return std::make_shared<Filter::HttpFilterConfigProviderManagerImpl>(); });
  return filter_config_provider_manager;
}

} // namespace Http
} // namespace Envoy
