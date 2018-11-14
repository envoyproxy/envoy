#pragma once

namespace Envoy {
// DRYs up the creation of a simple filter config for a filter that requires no config.
// TODO(snowp): make this reusable and use for other test filters.
template <class T>
class SimpleFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  SimpleFilterConfig() : EmptyHttpFilterConfig(T::name) {}

  Http::FilterFactoryCb createFilter(const std::string&, Server::Configuration::FactoryContext&) {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<T>());
    };
  }
};

} // namespace Envoy
