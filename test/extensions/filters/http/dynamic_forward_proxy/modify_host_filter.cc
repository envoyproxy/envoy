#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"

namespace Envoy {

class ModifyHostFilter : public Http::PassThroughFilter {
public:
  ModifyHostFilter() = default;
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    headers.setHost("non-existing.foo.bar.bats.com");
    return Http::FilterHeadersStatus::Continue;
  }
};

class ModifyHostFilterFactory : public Extensions::HttpFilters::Common::EmptyHttpDualFilterConfig {
public:
  ModifyHostFilterFactory() : EmptyHttpDualFilterConfig("modify-host-filter") {}
  absl::StatusOr<Http::FilterFactoryCb>
  createDualFilter(const std::string&, Server::Configuration::ServerFactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::ModifyHostFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<ModifyHostFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
