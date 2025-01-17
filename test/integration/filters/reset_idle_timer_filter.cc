#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A test filter that resets the stream idle timer via callbacks.
class ResetIdleTimerFilter : public Http::PassThroughFilter {
public:
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    decoder_callbacks_->resetIdleTimer();
    return Http::FilterDataStatus::Continue;
  }
};

class ResetIdleTimerFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  ResetIdleTimerFilterConfig() : EmptyHttpFilterConfig("reset-idle-timer-filter") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::ResetIdleTimerFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<ResetIdleTimerFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
