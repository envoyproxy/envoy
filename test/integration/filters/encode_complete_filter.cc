#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace EncodeCompleteFilter {

// A test filter that modifies the request header (i.e. map the cluster header
// to cluster name), clear the route cache.
class EncodeCompleteFilter : public Http::PassThroughFilter {
public:
  void encodeComplete() override {}
};

class EncodeCompleteFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  EncodeCompleteFilterConfig() : EmptyHttpFilterConfig("encode-complete-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(
          std::make_shared<::Envoy::EncodeCompleteFilter::EncodeCompleteFilter>());
    };
  }
};

// Perform static registration
static Registry::RegisterFactory<EncodeCompleteFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace EncodeCompleteFilter
} // namespace Envoy
