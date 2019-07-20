#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {

// A test filter that does StopIterationNoBuffer then continues after a 0ms alarm.
class StopIterationAndContinueFilter : public Http::PassThroughFilter {
public:
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override {
    RELEASE_ASSERT(!end_stream_seen_, "end stream seen twice");
    if (end_stream) {
      end_stream_seen_ = true;
      delay_timer_ = decoder_callbacks_->dispatcher().createTimer(
          [this]() -> void { decoder_callbacks_->continueDecoding(); });
      delay_timer_->enableTimer(std::chrono::seconds(0));
    }
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  Event::TimerPtr delay_timer_;
  bool end_stream_seen_{};
};

class StopIterationAndContinueFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  StopIterationAndContinueFilterConfig()
      : EmptyHttpFilterConfig("stop-iteration-and-continue-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::StopIterationAndContinueFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<StopIterationAndContinueFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
