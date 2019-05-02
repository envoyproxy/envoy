#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/stats/symbol_table_impl.h"
#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {

// A test filter that rejects all requests if CDS isn't ready yet.
class CdsReadyFilter : public Http::PassThroughFilter {
public:
  CdsReadyFilter(const Stats::Scope& root_scope)
      : root_scope_(root_scope),
        stat_name_("cluster_manager.cds.config_reload",
                   const_cast<Stats::SymbolTable&>(root_scope_.symbolTable())) {}
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override {
    if (root_scope_.getCounter(stat_name_.statName())->value() == 0) {
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }

private:
  const Stats::Scope& root_scope_;
  Stats::StatNameManagedStorage stat_name_;
};

class CdsReadyFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  CdsReadyFilterConfig() : EmptyHttpFilterConfig("cds-ready-filter") {}

  Http::FilterFactoryCb
  createFilter(const std::string&,
               Server::Configuration::FactoryContext& factory_context) override {
    return [&factory_context](Http::FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamFilter(
          std::make_shared<CdsReadyFilter>(factory_context.api().rootScope()));
    };
  }
};

static Registry::RegisterFactory<CdsReadyFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
