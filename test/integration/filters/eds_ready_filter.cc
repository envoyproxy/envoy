#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/stats/symbol_table.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A test filter that rejects all requests if EDS isn't healthy yet, and
// responds OK to all requests if it is.
class EdsReadyFilter : public Http::PassThroughFilter {
public:
  EdsReadyFilter(const Stats::Scope& root_scope, Stats::SymbolTable& symbol_table)
      : root_scope_(root_scope), stat_name_("cluster.cluster_0.membership_healthy", symbol_table) {}
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {

    // We must do the 'find' on the Store, which searches all scopes. Doing the
    // find only on the root scope will not find the gauge which is defined on a
    // lower-level scope.
    Stats::StatName stat_name = stat_name_.statName();
    Stats::GaugeOptConstRef gauge;
    root_scope_.constStore().forEachScope(nullptr, [&gauge, stat_name](const Stats::Scope& scope) {
      if (!gauge.has_value()) {
        gauge = scope.findGauge(stat_name);
      }
    });

    if (!gauge.has_value()) {
      decoder_callbacks_->sendLocalReply(Envoy::Http::Code::InternalServerError,
                                         "Couldn't find stat", nullptr, absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }
    if (gauge->get().value() == 0) {
      decoder_callbacks_->sendLocalReply(Envoy::Http::Code::InternalServerError, "EDS not ready",
                                         nullptr, absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }
    decoder_callbacks_->sendLocalReply(Envoy::Http::Code::OK, "EDS is ready", nullptr,
                                       absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

private:
  const Stats::Scope& root_scope_;
  Stats::StatNameManagedStorage stat_name_;
};

class EdsReadyFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  EdsReadyFilterConfig() : EmptyHttpFilterConfig("eds-ready-filter") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&,
               Server::Configuration::FactoryContext& factory_context) override {
    return [&factory_context](Http::FilterChainFactoryCallbacks& callbacks) {
      const Stats::Scope& scope = factory_context.api().rootScope();
      Stats::SymbolTable& symbol_table = factory_context.scope().symbolTable();
      callbacks.addStreamFilter(std::make_shared<EdsReadyFilter>(scope, symbol_table));
    };
  }
};

static Registry::RegisterFactory<EdsReadyFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
