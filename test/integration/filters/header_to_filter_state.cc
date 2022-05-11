#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/header_to_filter_state.pb.h"
#include "test/integration/filters/header_to_filter_state.pb.validate.h"

namespace Envoy {

const char HeaderToFilterStateFilterName[] = "envoy.filters.http.header_to_filter_state_for_test";
// This filter extracts a string header from "header" and
// save it into FilterState as name "state" as read-only Router::StringAccessor.
class HeaderToFilterStateFilter : public Http::PassThroughDecoderFilter {
public:
  HeaderToFilterStateFilter(const std::string& header, const std::string& state)
      : header_(header), state_(state) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    const auto entry = headers.get(header_);
    if (!entry.empty()) {
      decoder_callbacks_->streamInfo().filterState()->setData(
          state_, std::make_unique<Router::StringAccessorImpl>(entry[0]->value().getStringView()),
          StreamInfo::FilterState::StateType::ReadOnly,
          StreamInfo::FilterState::LifeSpan::FilterChain);
    }
    return Http::FilterHeadersStatus::Continue;
  }

private:
  Http::LowerCaseString header_;
  std::string state_;
};

class HeaderToFilterStateFilterConfig : public Extensions::HttpFilters::Common::FactoryBase<
                                 test::integration::filters::HeaderToFilterStateFilterConfig> {
public:
  HeaderToFilterStateFilterConfig()
      : FactoryBase("header-to-filter-state") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::HeaderToFilterStateFilterConfig& proto_config, const std::string&,
      Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(
          std::make_shared<HeaderToFilterStateFilter>(proto_config.header_name(), proto_config.));
    };
  }
};

REGISTER_FACTORY(AddBodyFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
} // namespace Envoy
