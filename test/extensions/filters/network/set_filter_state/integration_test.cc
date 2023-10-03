#include <memory>

#include "envoy/extensions/filters/network/set_filter_state/v3/set_filter_state.pb.h"
#include "envoy/extensions/filters/network/set_filter_state/v3/set_filter_state.pb.validate.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/network/set_filter_state/config.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
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
    envoy::extensions::filters::network::set_filter_state::v3::Config proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);
    auto config = std::make_shared<Filters::Common::SetFilterState::Config>(
        proto_config.on_new_connection(), StreamInfo::FilterState::LifeSpan::Connection, context_);
    auto filter = std::make_shared<SetFilterState>(config);
    NiceMock<Network::MockReadFilterCallbacks> callbacks;
    filter->initializeReadFilterCallbacks(callbacks);
    EXPECT_CALL(callbacks.connection_, streamInfo()).WillRepeatedly(ReturnRef(info_));
    EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<StreamInfo::MockStreamInfo> info_;
};

TEST_F(SetMetadataIntegrationTest, FromHeader) {
  const std::string yaml_config = R"EOF(
  on_new_connection:
  - object_key: foo
    format_string:
      text_format_source:
        inline_string: "bar"
  )EOF";
  runFilter(yaml_config);
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  ASSERT_NE(nullptr, foo);
  EXPECT_EQ(foo->serializeAsString(), "bar");
}

} // namespace SetFilterState
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
