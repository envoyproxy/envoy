#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

// A filter that calls Http::FilterHeadersStatus::Continue and set metadata after a local reply.
class ContinueAfterLocalReplyWithMetadataFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "continue-after-local-reply-with-metadata-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    Http::MetadataMap metadata_map = {{"headers", "headers"}, {"duplicate", "duplicate"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->addDecodedMetadata().emplace_back(std::move(metadata_map_ptr));
    decoder_callbacks_->sendLocalReply(Envoy::Http::Code::OK, "", nullptr, absl::nullopt,
                                       "ContinueAfterLocalReplyFilter is ready");
    return Http::FilterHeadersStatus::Continue;
  }

  // Adds new metadata to metadata_map directly
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override {
    metadata_map["data"] = "data";
    throw EnvoyException("Should not call decode metadata after local reply.");
    return Http::FilterMetadataStatus::Continue;
  }
};

constexpr char ContinueAfterLocalReplyWithMetadataFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<ContinueAfterLocalReplyWithMetadataFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
