#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/test_filters.pb.h"

namespace Envoy {

// A test filter that reproduces the filter-manager data-loss scenario from
// https://github.com/envoyproxy/envoy/issues/46841
//
// The filter iterates on headers, then on the first body frame moves that frame
// into the filter-manager buffer via addDecodedData()/addEncodedData() and
// returns Continue. Because the filter is already iterating, this exercises the
// commonHandleAfterDataCallback() Continue path where the just-buffered frame
// must be forwarded down the chain rather than the now-empty frame. Subsequent
// frames are passed through untouched. Chaining this ahead of another filter
// that inspects the body (e.g. ext_proc in FULL_DUPLEX_STREAMED mode) surfaces
// the dropped frame as a body-integrity mismatch when the fix is disabled.
class AddDataAndContinueFilter : public Http::PassThroughFilter {
public:
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    if (!decoded_first_frame_) {
      decoded_first_frame_ = true;
      decoder_callbacks_->addDecodedData(data, false);
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool) override {
    if (!encoded_first_frame_) {
      encoded_first_frame_ = true;
      encoder_callbacks_->addEncodedData(data, false);
    }
    return Http::FilterDataStatus::Continue;
  }

private:
  bool decoded_first_frame_{false};
  bool encoded_first_frame_{false};
};

class AddDataAndContinueFilterConfig
    : public Extensions::HttpFilters::Common::UniqueEmptyHttpFilterConfig<
          test::integration::filters::AddDataAndContinueFilterConfig> {
public:
  AddDataAndContinueFilterConfig()
      : UniqueEmptyHttpFilterConfig<test::integration::filters::AddDataAndContinueFilterConfig>(
            "add-data-and-continue-filter") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::AddDataAndContinueFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<AddDataAndContinueFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
