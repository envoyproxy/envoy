#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

class LocalReplyDuringEncodeData : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "local-reply-during-encode-data";

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    encoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "", nullptr, absl::nullopt,
                                       "");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
};

constexpr char LocalReplyDuringEncodeData::name[];
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyDuringEncodeData>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
