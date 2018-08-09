#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"

#include "test/integration/add_trailers_filter.h"

namespace Envoy {
class AddTrailersStreamFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  AddTrailersStreamFilterConfig() : EmptyHttpFilterConfig("add-trailers-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&, Server::Configuration::FactoryContext&);
};
} // namespace Envoy
