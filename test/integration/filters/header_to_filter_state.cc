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
                            test::integration::filters::SharingConfig shared, bool upstream_filter)
      : header_(header), state_(state), read_only_(read_only), shared_(shared),
        upstream_filter_(upstream_filter) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    const auto entry = headers.get(header_);
    if (!entry.empty()) {
      auto shared = StreamInfo::StreamSharingMayImpactPooling::None;
      switch (shared_) {
      case test::integration::filters::SharingConfig::ONCE:
        shared = StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce;
        break;
      case test::integration::filters::SharingConfig::TRANSITIVE:
        shared = StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection;
        break;
      default:
        break;
      }
      auto& filter_state =
          upstream_filter_ ? decoder_callbacks_->streamInfo().upstreamInfo()->upstreamFilterState()
                           : decoder_callbacks_->streamInfo().filterState();
      filter_state->setData(
          state_, std::make_unique<Router::StringAccessorImpl>(entry[0]->value().getStringView()),
          read_only_ ? StreamInfo::FilterState::StateType::ReadOnly
                     : StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::FilterChain, shared);
    }
    return Http::FilterHeadersStatus::Continue;
  }

private:
  const Http::LowerCaseString header_;
  const std::string state_;
  const bool read_only_;
  const test::integration::filters::SharingConfig shared_;
  const bool upstream_filter_;
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
          proto_config.shared(), /*upstream_filter=*/false));
    };
  }
};

class HeaderToFilterStateFilterUpstreamFactory
    : public Extensions::HttpFilters::Common::UpstreamFactoryBase<
          test::integration::filters::HeaderToFilterStateFilterConfig> {
public:
  HeaderToFilterStateFilterUpstreamFactory()
      : UpstreamFactoryBase("header-to-filter-state-upstream") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::HeaderToFilterStateFilterConfig& proto_config,
      const std::string&, Server::Configuration::UpstreamHttpFactoryContext&) override {
    return [=](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<HeaderToFilterStateFilter>(
          proto_config.header_name(), proto_config.state_name(), proto_config.read_only(),
          proto_config.shared(), true));
    };
  }
};

REGISTER_FACTORY(HeaderToFilterStateFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(HeaderToFilterStateFilterUpstreamFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);
} // namespace Envoy
