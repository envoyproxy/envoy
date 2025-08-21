#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter that waits for the request/response to finish before continuing.
class AddInvalidDataFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "add-invalid-data-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    if (!headers.get(Envoy::Http::LowerCaseString("invalid-encode")).empty()) {
      added_invalid_encode_ = true;
      Buffer::OwnedImpl body("body");
      encoder_callbacks_->addEncodedData(body, false);
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    // If we made an invalid call to addEncodedData during decodeHeaders, we
    // should not clobber the generated local reply.
    if (!added_invalid_encode_) {
      Buffer::OwnedImpl body("body");
      decoder_callbacks_->addDecodedData(body, false);
    }
    return Http::FilterHeadersStatus::Continue;
  }

private:
  // Tracks whether the downstream requested the filter to make an invalid call
  // to addEncodedData during decoding headers.
  bool added_invalid_encode_{false};
};

constexpr char AddInvalidDataFilter::name[];

static Registry::RegisterFactory<SimpleFilterConfig<AddInvalidDataFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    encoder_register_;
static Registry::RegisterFactory<SimpleFilterConfig<AddInvalidDataFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    encoder_register_upstream_;
} // namespace Envoy
