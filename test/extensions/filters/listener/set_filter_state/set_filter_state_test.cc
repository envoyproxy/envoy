#include "envoy/extensions/filters/listener/set_filter_state/v3/set_filter_state.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/listener_filter_buffer_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/listener/set_filter_state/config.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace SetFilterState {
namespace {

using testing::NiceMock;
using testing::ReturnRef;

class SetFilterStateTest : public testing::Test {
public:
  void initialize(const std::string& yaml) {
    envoy::extensions::filters::listener::set_filter_state::v3::Config proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    on_accept_config_ = std::make_shared<Filters::Common::SetFilterState::Config>(
        proto_config.on_accept(), StreamInfo::FilterState::LifeSpan::Connection, context_);
    filter_ = std::make_unique<SetFilterState>(on_accept_config_);
    filter_->onAccept(cb_);
    EXPECT_EQ(0, filter_->maxReadBytes());
    {
      NiceMock<Network::MockIoHandle> io_handle;
      NiceMock<Event::MockDispatcher> dispatcher;
      Network::ListenerFilterBufferImpl buffer(
          io_handle, dispatcher, [](bool) {}, [](Network::ListenerFilterBuffer&) {}, false, 1);
      EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(buffer));
    }
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Filters::Common::SetFilterState::ConfigSharedPtr on_accept_config_;
  std::unique_ptr<SetFilterState> filter_;
  NiceMock<Network::MockListenerFilterCallbacks> cb_;
};

TEST_F(SetFilterStateTest, SetFilterState) {
  const std::string yaml = R"EOF(
on_accept:
  - object_key: test_key
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "test_value"
)EOF";
  initialize(yaml);

  EXPECT_EQ("test_value", cb_.stream_info_.filterState()
                              ->getDataReadOnly<Router::StringAccessor>("test_key")
                              ->asString());
}

} // namespace
} // namespace SetFilterState
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
