#include <string>

#include "envoy/registry/registry.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "contrib/golang/common/dso/test/mocks.h"
#include "contrib/golang/filters/network/source/golang.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {
namespace {

class FilterTest : public testing::Test {
public:
  FilterTest() { ENVOY_LOG_MISC(info, "test"); }
  ~FilterTest() override = default;

  void initialize() {
    proto_config_ = prepareProtoConfig();
    config_ = std::make_shared<FilterConfig>(proto_config_);
    dso_ = std::make_shared<Dso::MockNetworkFilterDsoImpl>();
    filter_ = std::make_shared<Filter>(context_, config_, 1, dso_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  envoy::extensions::filters::network::golang::v3alpha::Config prepareProtoConfig() {
    const auto yaml_fmt = R"EOF(
  library_id: %s
  library_path: %s
  is_terminal_filter: true
  plugin_name: xxx
  plugin_config:
    "@type": type.googleapis.com/udpa.type.v1.TypedStruct
    type_url: typexx
    value:
        key: value
        int: 10
  )EOF";

    auto yaml_string = absl::StrFormat(
        yaml_fmt, "test",
        TestEnvironment::substitute(
            "{{ test_rundir }}/contrib/golang/filters/network/test/test_data/filter.so"));
    envoy::extensions::filters::network::golang::v3alpha::Config proto_config;
    TestUtility::loadFromYaml(yaml_string, proto_config);
    return proto_config;
  }

  FilterConfigSharedPtr config_;
  envoy::extensions::filters::network::golang::v3alpha::Config proto_config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<Dso::MockNetworkFilterDsoImpl> dso_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  FilterSharedPtr filter_;
};

TEST_F(FilterTest, InvokeDsoOnEventOrData) {
  initialize();

  EXPECT_CALL(*dso_.get(), envoyGoFilterOnDownstreamConnection(_, _, _, _));
  filter_->onNewConnection();

  EXPECT_CALL(*dso_.get(),
              envoyGoFilterOnDownstreamEvent(_, GoInt(Network::ConnectionEvent::Connected)));
  filter_->onEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl someData("123");
  EXPECT_CALL(*dso_.get(), envoyGoFilterOnDownstreamData(_, someData.length(), _, _, _))
      .WillOnce(Return(GoUint64(Network::FilterStatus::Continue)));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(someData, false));

  EXPECT_CALL(*dso_.get(), envoyGoFilterOnDownstreamWrite(_, someData.length(), _, _, _))
      .WillOnce(Return(GoUint64(Network::FilterStatus::Continue)));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(someData, false));
}

TEST_F(FilterTest, FilterState) {
  initialize();
  EXPECT_CALL(*dso_.get(), envoyGoFilterOnDownstreamConnection(_, _, _, _));
  filter_->onNewConnection();
  filter_->setFilterState("filterStateKey", "filterStateValue",
                          static_cast<int>(StreamInfo::FilterState::StateType::Mutable),
                          static_cast<int>(StreamInfo::FilterState::LifeSpan::Connection),
                          static_cast<int>(StreamInfo::StreamSharingMayImpactPooling::None));
  GoString str;
  filter_->getFilterState("filterStateKey", &str);
  EXPECT_EQ("filterStateValue", std::string(str.p, str.n));
}

TEST_F(FilterTest, WriteAndClose) {
  initialize();

  Buffer::OwnedImpl someData("123");
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false));
  filter_->write(someData, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(_, "go_downstream_close"));
  EXPECT_CALL(*dso_.get(), envoyGoFilterOnDownstreamEvent(_, _));
  filter_->close(Network::ConnectionCloseType::NoFlush);

  // once filter got closed, should not write any more
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(0);
  filter_->write(someData, false);

  // once filter got closed, should not close any more
  EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(0);
  filter_->close(Network::ConnectionCloseType::NoFlush);
}

} // namespace
} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
