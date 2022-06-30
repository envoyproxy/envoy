#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/network/filter_state_socket_tag.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/mocks/network/socket_tag.h"

namespace Envoy {

// A filter that looks for a specific header, and takes the address from that
// header and inserts proxy override metadata.
class HeaderToSocketTagFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_headers, bool) override {
    auto socket_tag = Http::LowerCaseString("socket-tag");
    if (!request_headers.get(socket_tag).empty()) {
      auto tag = std::make_shared<Network::MockSocketTag>();
      std::string tag_string(request_headers.get(socket_tag)[0]->value().getStringView());
      EXPECT_CALL(*tag, hashKey(testing::_)).WillRepeatedly(testing::Invoke([=](std::vector<uint8_t>& key) {
        pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(tag_string), key);
      }));
      decoder_callbacks_->streamInfo().filterState()->setData(
          Network::FilterStateSocketTag::key(),
        std::make_unique<Network::FilterStateSocketTag>(tag),
        StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::FilterChain);
      request_headers.remove(socket_tag);
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

class HeaderToSocketTagFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  HeaderToSocketTagFilterConfig() : EmptyHttpFilterConfig("header-to-socket-tag-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::HeaderToSocketTagFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<HeaderToSocketTagFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
