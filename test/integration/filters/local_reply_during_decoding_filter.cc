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
      local_reply_skipped_ = true;
      return Http::FilterHeadersStatus::Continue;
    }
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "", nullptr, absl::nullopt,
                                       "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Due to the above local reply, this method should never be invoked in tests.
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    ASSERT(local_reply_skipped_);
    return Http::FilterDataStatus::Continue;
  }

  // Due to the above local reply, this method should never be invoked in tests.
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap&) override {
    ASSERT(local_reply_skipped_);
    return Http::FilterMetadataStatus::Continue;
  }

private:
  bool local_reply_skipped_ = false;
};

constexpr char LocalReplyDuringDecode::name[];
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyDuringDecode>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyDuringDecode>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
