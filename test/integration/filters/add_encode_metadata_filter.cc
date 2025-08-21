#include <chrono>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

// A filter add encoded metadata in encodeHeaders.
class AddEncodeMetadataFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "add-metadata-encode-headers-filter";

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    Http::MetadataMap metadata_map = {{"headers", "headers"}, {"duplicate", "duplicate"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata(std::move(metadata_map_ptr));
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
};

constexpr char AddEncodeMetadataFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<AddEncodeMetadataFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
