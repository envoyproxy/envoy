#pragma once

#include <string>

#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// `DRYs` up the creation of a simple filter config for a filter that requires no config.
template <class T>
class SimpleFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpDualFilterConfig {
public:
  SimpleFilterConfig() : EmptyHttpDualFilterConfig(T::name) {}

  Http::FilterFactoryCb createDualFilter(const std::string&,
                                         Server::Configuration::ServerFactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<T>());
    };
  }
};

} // namespace Envoy
