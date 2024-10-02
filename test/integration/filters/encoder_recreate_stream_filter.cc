#include <chrono>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

class EncoderRecreateStreamFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "encoder-recreate-stream-filter";

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    const auto* filter_state =
        decoder_callbacks_->streamInfo().filterState()->getDataReadOnly<Router::StringAccessorImpl>(
            "test_key");

    if (filter_state != nullptr) {
      return ::Envoy::Http::FilterHeadersStatus::Continue;
    }

    decoder_callbacks_->streamInfo().filterState()->setData(
        "test_key", std::make_unique<Router::StringAccessorImpl>("test_value"),
        StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Request);

    if (decoder_callbacks_->recreateStream(nullptr)) {
      return ::Envoy::Http::FilterHeadersStatus::StopIteration;
    }

    return ::Envoy::Http::FilterHeadersStatus::Continue;
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }
};

// perform static registration
constexpr char EncoderRecreateStreamFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<EncoderRecreateStreamFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
