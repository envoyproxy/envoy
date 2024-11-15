#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter that:
// - Allows headers through (but NOT headers-only requests/responses).
// - Internally buffers all body data.
// - When end stream occurs (either with/without trailers), injects data to the
//   next filter asynchronously.
// - Continues filter iteration if trailers were present.
class AsyncInjectBodyAtEndStreamFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "async-inject-body-at-end-stream-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override {
    ASSERT(!end_stream);
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override {
    ASSERT(Http::Utility::getResponseStatus(headers) == 200);
    ASSERT(!end_stream);
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    request_buffer_.move(data);
    if (end_stream) {
      doRequestInjection(/*has_trailers=*/false);
    }
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    response_buffer_.move(data);
    if (end_stream) {
      doResponseInjection(/*has_trailers=*/false);
    }
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    doRequestInjection(/*has_trailers=*/true);
    return Http::FilterTrailersStatus::StopIteration;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    doResponseInjection(/*has_trailers=*/true);
    return Http::FilterTrailersStatus::StopIteration;
  }

private:
  void doRequestInjection(bool has_trailers) {
    decoder_callbacks_->dispatcher().post([this, has_trailers]() -> void {
      // Emulate multiple injections to next filter.
      Envoy::Buffer::OwnedImpl split_request;
      split_request.move(request_buffer_, 1);
      decoder_callbacks_->injectDecodedDataToFilterChain(split_request, /*end_stream=*/false);
      decoder_callbacks_->injectDecodedDataToFilterChain(request_buffer_,
                                                         /*end_stream=*/!has_trailers);
      if (has_trailers) {
        decoder_callbacks_->continueDecoding();
      }
    });
  }

  void doResponseInjection(bool has_trailers) {
    encoder_callbacks_->dispatcher().post([this, has_trailers]() -> void {
      // Emulate multiple injections to next filter.
      Envoy::Buffer::OwnedImpl split_response;
      split_response.move(response_buffer_, 1);
      encoder_callbacks_->injectEncodedDataToFilterChain(split_response, /*end_stream=*/false);
      encoder_callbacks_->injectEncodedDataToFilterChain(response_buffer_,
                                                         /*end_stream=*/!has_trailers);
      if (has_trailers) {
        encoder_callbacks_->continueEncoding();
      }
    });
  }

  Envoy::Buffer::OwnedImpl request_buffer_;
  Envoy::Buffer::OwnedImpl response_buffer_;
};

static Registry::RegisterFactory<SimpleFilterConfig<AsyncInjectBodyAtEndStreamFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
} // namespace Envoy
