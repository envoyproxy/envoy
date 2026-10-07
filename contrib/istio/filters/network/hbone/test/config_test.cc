#include "source/common/stream_info/bool_accessor_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/istio/filters/network/hbone/source/config.h"
#include "contrib/istio/filters/network/hbone/source/hbone.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {
namespace {

class HboneConfigTest : public testing::Test {
protected:
  HboneConfigTest() {
    TestUtility::loadFromYaml(R"EOF(
tcp_proxy:
  stat_prefix: hbone
  cluster: hbone
  tunneling_config:
    hostname: "app:8080"
)EOF",
                              config_);
  }
  FilterConfig config_;
  FilterConfigFactory factory_;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(HboneConfigTest, RequiresConnect) {
  config_.mutable_tcp_proxy()->clear_tunneling_config();
  EXPECT_FALSE(factory_.createFilterFactoryFromProto(config_, context_).ok());
}

TEST_F(HboneConfigTest, RejectsPostTunneling) {
  config_.mutable_tcp_proxy()->mutable_tunneling_config()->set_use_post(true);
  EXPECT_FALSE(factory_.createFilterFactoryFromProto(config_, context_).ok());
}

TEST_F(HboneConfigTest, RejectsWaitingForApplicationData) {
  config_.mutable_tcp_proxy()->set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  EXPECT_FALSE(factory_.createFilterFactoryFromProto(config_, context_).ok());
}

TEST_F(HboneConfigTest, AcceptsConnectConfiguration) {
  EXPECT_TRUE(factory_.createFilterFactoryFromProto(config_, context_).ok());
}

class TestFilter : public Filter {
public:
  using Filter::Filter;
  void upstreamEvent(Network::ConnectionEvent event) { upstream_callbacks_->onEvent(event); }
};

class HboneFilterTest : public HboneConfigTest {
protected:
  HboneFilterTest() {
    EXPECT_CALL(callbacks_, connection())
        .WillRepeatedly(testing::ReturnRef(callbacks_.connection_));
  }

  void initialize(bool paired = true) {
    if (paired) {
      callbacks_.connection_.stream_info_.filterState()->setData(ConnectState::Key, state_);
    }
    filter_ = std::make_unique<TestFilter>(
        std::make_shared<TcpProxy::Config>(config_.tcp_proxy(), context_),
        context_.server_factory_context_.cluster_manager_);
    filter_->initializeReadFilterCallbacks(callbacks_);
  }

  testing::StrictMock<Network::MockReadFilterCallbacks> callbacks_;
  ConnectStateSharedPtr state_{std::make_shared<ConnectState>()};
  std::unique_ptr<TestFilter> filter_;
};

TEST_F(HboneFilterTest, RequiresPairedTransport) {
  initialize(false);
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, testing::_));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
}

TEST_F(HboneFilterTest, RejectsDisabledTunneling) {
  initialize();
  callbacks_.connection_.stream_info_.filterState()->setData(
      TcpProxy::DisableTunnelingFilterStateKey,
      std::make_shared<StreamInfo::BoolAccessorImpl>(true));
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, testing::_));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  EXPECT_EQ(ConnectState::Phase::Failed, state_->phase());
}

TEST_F(HboneFilterTest, UpstreamCloseCancelsAcceptanceBeforeDownstreamFlushCompletes) {
  initialize();
  state_->accept();
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
      .WillOnce([this](Network::ConnectionCloseType) {
        // Model buffered server data: no downstream close event has fired yet.
        EXPECT_EQ(ConnectState::Phase::Failed, state_->phase());
      });
  filter_->upstreamEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(ConnectState::Phase::Failed, state_->phase());
}

TEST_F(HboneFilterTest, EstablishedUpstreamCloseKeepsNormalFlushSemantics) {
  initialize();
  state_->accept();
  state_->connected();
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
      .WillOnce([this](Network::ConnectionCloseType) {
        EXPECT_EQ(ConnectState::Phase::Connected, state_->phase());
      });
  filter_->upstreamEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(ConnectState::Phase::Connected, state_->phase());
}

} // namespace
} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
