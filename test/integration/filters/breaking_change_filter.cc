#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/runtime/breaking_changes.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"
#include "test/integration/filters/test_filters.pb.h"

namespace Envoy {

using Runtime::BreakingChangesTracker;

class BreakingChangeFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "breaking-change-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    static const Http::LowerCaseString test_enabled_header("test_enabled_breaking_change");
    static const Http::LowerCaseString test_disabled_header("test_disabled_breaking_change");

    if (!headers.get(test_enabled_header).empty()) {
      RECORD_BREAKING_CHANGE(test_enabled_breaking_change,
                             decoder_callbacks_->streamInfo().filterState());
    }
    if (!headers.get(test_disabled_header).empty()) {
      RECORD_BREAKING_CHANGE(test_disabled_breaking_change,
                             decoder_callbacks_->streamInfo().filterState());
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char BreakingChangeFilter::name[];
static Registry::RegisterFactory<
    UniqueSimpleFilterConfig<BreakingChangeFilter,
                             test::integration::filters::BreakingChangeFilterConfig>,
    Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<
    UniqueSimpleFilterConfig<BreakingChangeFilter,
                             test::integration::filters::BreakingChangeFilterConfig>,
    Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
