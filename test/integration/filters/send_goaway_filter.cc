#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

class GoAwayDuringDecoding : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "send-goaway-during-decode-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_headers, bool) override {
    auto result = request_headers.get(Http::LowerCaseString("skip-goaway"));
    if (!result.empty() && result[0]->value() == "true") {
      go_away_skiped_ = true;
      return Http::FilterHeadersStatus::Continue;
    }
    decoder_callbacks_->sendGoAwayandClose();
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Due to the above local reply, this method should never be invoked in tests.
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    ASSERT(go_away_skiped_);
    return Http::FilterDataStatus::Continue;
  }

  // Due to the above local reply, this method should never be invoked in tests.
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap&) override {
    ASSERT(go_away_skiped_);
    return Http::FilterMetadataStatus::Continue;
  }

private:
  bool go_away_skiped_ = false;
};

constexpr char GoAwayDuringDecoding::name[];
static Registry::RegisterFactory<SimpleFilterConfig<GoAwayDuringDecoding>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
