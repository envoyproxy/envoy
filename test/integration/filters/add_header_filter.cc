

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"

namespace Envoy {

class AddHeaderFilter : public Http::PassThroughFilter {
public:
  static constexpr absl::string_view kHeader = "x-header-to-add";
  AddHeaderFilter() = default;
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    headers.addCopy(Http::LowerCaseString(kHeader), "value");
    return Http::FilterHeadersStatus::Continue;
  }
};

class AddHeaderFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpDualFilterConfig {
public:
  AddHeaderFilterConfig() : EmptyHttpDualFilterConfig("add-header-filter") {}
  Http::FilterFactoryCb createDualFilter(const std::string&,
                                         Server::Configuration::ServerFactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::AddHeaderFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<AddHeaderFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<AddHeaderFilterConfig,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
