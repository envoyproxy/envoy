#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/header_to_filter_state.pb.h"
#include "test/integration/filters/header_to_filter_state.pb.validate.h"

namespace Envoy {

// This filter extracts a string header from "header" and
// save it into FilterState as name "state" as read-only Router::StringAccessor.
class HeaderToFilterStateFilter : public Http::PassThroughDecoderFilter {
public:
  HeaderToFilterStateFilter(const std::string& header, const std::string& state, bool read_only,
                            bool shared)
      : header_(header), state_(state), read_only_(read_only), shared_(shared) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    const auto entry = headers.get(header_);
    if (!entry.empty()) {
      decoder_callbacks_->streamInfo().filterState()->setData(
          state_, std::make_unique<Router::StringAccessorImpl>(entry[0]->value().getStringView()),
          read_only_ ? StreamInfo::FilterState::StateType::ReadOnly
                     : StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::FilterChain,
          shared_ ? StreamInfo::FilterState::StreamSharing::SharedWithUpstreamConnection
                  : StreamInfo::FilterState::StreamSharing::None);
    }
    return Http::FilterHeadersStatus::Continue;
  }

private:
  Http::LowerCaseString header_;
  std::string state_;
  bool read_only_;
  bool shared_;
};

class HeaderToFilterStateFilterFactory
    : public Extensions::HttpFilters::Common::FactoryBase<
          test::integration::filters::HeaderToFilterStateFilterConfig> {
public:
  HeaderToFilterStateFilterFactory() : FactoryBase("header-to-filter-state") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::HeaderToFilterStateFilterConfig& proto_config,
      const std::string&, Server::Configuration::FactoryContext&) override {
    return [=](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<HeaderToFilterStateFilter>(
          proto_config.header_name(), proto_config.state_name(), proto_config.read_only(),
          proto_config.shared()));
    };
  }
};

REGISTER_FACTORY(HeaderToFilterStateFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
} // namespace Envoy
