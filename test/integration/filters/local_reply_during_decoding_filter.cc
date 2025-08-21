#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

class LocalReplyDuringDecode : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "local-reply-during-decode";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_headers, bool) override {
    auto result = request_headers.get(Http::LowerCaseString("skip-local-reply"));
    if (!result.empty() && result[0]->value() == "true") {
      header_local_reply_skipped_ = true;
      return Http::FilterHeadersStatus::Continue;
    }
    result = request_headers.get(Http::LowerCaseString("local-reply-during-data"));
    if (!result.empty() && result[0]->value() == "true") {
      local_reply_during_data_ = true;
      header_local_reply_skipped_ = true;
      return Http::FilterHeadersStatus::Continue;
    }
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "", nullptr, absl::nullopt,
                                       "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    ASSERT(header_local_reply_skipped_);
    if (local_reply_during_data_) {
      decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "", nullptr,
                                         absl::nullopt, "");
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap&) override {
    ASSERT(header_local_reply_skipped_);
    return Http::FilterMetadataStatus::Continue;
  }

private:
  bool header_local_reply_skipped_ = false;
  bool local_reply_during_data_ = false;
};

constexpr char LocalReplyDuringDecode::name[];
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyDuringDecode>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyDuringDecode>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
