#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

// A filter that calls Http::FilterHeadersStatus::StopAll and set metadata after a local reply.
class LocalReplyWithMetadataFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "local-reply-with-metadata-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    Http::MetadataMap metadata_map = {{"headers", "headers"}, {"duplicate", "duplicate"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->addDecodedMetadata().emplace_back(std::move(metadata_map_ptr));
    decoder_callbacks_->sendLocalReply(Envoy::Http::Code::OK, "", nullptr, absl::nullopt,
                                       "local_reply_with_metadata_filter_is_ready");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Adds new metadata to metadata_map directly
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override {
    metadata_map["data"] = "data";
    ASSERT(false);
    return Http::FilterMetadataStatus::Continue;
  }
};

constexpr char LocalReplyWithMetadataFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyWithMetadataFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyWithMetadataFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
