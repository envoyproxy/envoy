#pragma once
#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {
struct TrailersAfterBufferedDataFilter : Http::PassThroughFilter {
  explicit TrailersAfterBufferedDataFilter(bool encoding_path) : encoding_path_(encoding_path) {}

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    if (encoding_path_) {
      return Http::FilterDataStatus::Continue;
    }

    if (end_stream) {
      data.move(buffer_);
      decoder_callbacks_->addDecodedTrailers();
      return Http::FilterDataStatus::Continue;
    }

    buffer_.move(data);
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    if (!encoding_path_) {
      return Http::FilterDataStatus::Continue;
    }

    if (end_stream) {
      data.move(buffer_);
      encoder_callbacks_->addEncodedTrailers();
      return Http::FilterDataStatus::Continue;
    }

    buffer_.move(data);
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  bool encoding_path_;
  Buffer::OwnedImpl buffer_;
};

class TrailersAfterBufferedDataFilterFactory
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  TrailersAfterBufferedDataFilterFactory(bool encoding_path)
      : EmptyHttpFilterConfig("inject_trailers"), encoding_path_(encoding_path) {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<TrailersAfterBufferedDataFilter>(encoding_path_));
    };
  }

private:
  bool encoding_path_;
};
} // namespace Envoy