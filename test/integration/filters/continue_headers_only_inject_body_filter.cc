#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter that continues iteration of headers-only request/response without ending the
// stream, then injects a body later.
class ContinueHeadersOnlyInjectBodyFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "continue-headers-only-inject-body-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override {
    headers.setContentLength(body_.length());
    decoder_callbacks_->dispatcher().post([this]() -> void {
      request_injected_ = true;
      Buffer::OwnedImpl buffer(body_);
      decoder_callbacks_->injectDecodedDataToFilterChain(buffer, true);
      if (request_decoded_) {
        decoder_callbacks_->continueDecoding();
      }
    });
    if (end_stream) {
      return Http::FilterHeadersStatus::ContinueAndDontEndStream;
    } else {
      return Http::FilterHeadersStatus::Continue;
    }
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override {
    headers.setContentLength(body_.length());
    encoder_callbacks_->dispatcher().post([this, end_stream]() -> void {
      response_injected_ = true;
      Buffer::OwnedImpl buffer(body_);
      encoder_callbacks_->injectEncodedDataToFilterChain(buffer, end_stream);
      if (response_encoded_) {
        encoder_callbacks_->continueEncoding();
      }
    });
    if (end_stream) {
      return Http::FilterHeadersStatus::ContinueAndDontEndStream;
    } else {
      return Http::FilterHeadersStatus::Continue;
    }
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    response_encoded_ = true;
    if (!response_injected_) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    request_decoded_ = true;
    if (!request_injected_) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
    return Http::FilterDataStatus::Continue;
  }

private:
  constexpr static absl::string_view body_ = "body";
  // For HTTP/3, the headers and fin will arrive separately.
  // Make sure that the body is added, and then continue encoding/decoding occurs once.
  // If encodeData/decodeData hits before the post, encodeData/decodeData will stop iteration to
  // ensure the fin is not passed on, and when the post happens it will resume encoding. If the post
  // happens first, encodeData/decodeData can simply continue.
  bool request_injected_{};
  bool request_decoded_{};
  bool response_injected_{};
  bool response_encoded_{};
};

static Registry::RegisterFactory<SimpleFilterConfig<ContinueHeadersOnlyInjectBodyFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<ContinueHeadersOnlyInjectBodyFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;
} // namespace Envoy
