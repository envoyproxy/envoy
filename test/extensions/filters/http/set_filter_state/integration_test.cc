#include <memory>

#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/set_filter_state/config.h"
#include "source/server/generic_factory_context.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetFilterState {

class ObjectFooFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "foo"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(ObjectFooFactory, StreamInfo::FilterState::ObjectFactory);

class SetMetadataIntegrationTest : public testing::Test {
public:
  SetMetadataIntegrationTest() = default;

  void runFilter(const std::string& yaml_config) {
    envoy::extensions::filters::http::set_filter_state::v3::Config proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    // Test the factory method.
    {
      SetFilterStateConfig factory;
      auto cb_1 = factory.createFilterFactoryFromProto(proto_config, "", context_);
      auto cb_2 = factory.createFilterFactoryFromProtoWithServerContext(
          proto_config, "", context_.server_factory_context_);

      NiceMock<Http::MockFilterChainFactoryCallbacks> filter_chain_factory_callbacks;

      EXPECT_CALL(filter_chain_factory_callbacks, addStreamDecoderFilter(_)).Times(2);
      cb_1.value()(filter_chain_factory_callbacks);
      cb_2(filter_chain_factory_callbacks);
    }

    Server::GenericFactoryContextImpl generic_context(context_);

    auto config = std::make_shared<Filters::Common::SetFilterState::Config>(
        proto_config.on_request_headers(), StreamInfo::FilterState::LifeSpan::FilterChain,
        generic_context);
    auto filter = std::make_shared<SetFilterState>(config);
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    filter->setDecoderFilterCallbacks(decoder_callbacks);
    EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(info_));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(headers_, true));
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Http::TestRequestHeaderMapImpl headers_{{"test-header", "test-value"}};
  NiceMock<StreamInfo::MockStreamInfo> info_;
};

TEST_F(SetMetadataIntegrationTest, FromHeader) {
  const std::string yaml_config = R"EOF(
  on_request_headers:
  - object_key: foo
    format_string:
      text_format_source:
        inline_string: "%REQ(test-header)%"
  )EOF";
  runFilter(yaml_config);
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  ASSERT_NE(nullptr, foo);
  EXPECT_EQ(foo->serializeAsString(), "test-value");
}

} // namespace SetFilterState
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
