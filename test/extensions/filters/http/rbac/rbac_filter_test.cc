#include "common/network/utility.h"

#include "extensions/filters/http/rbac/rbac_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/extensions/filters/common/rbac/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {
namespace {

class RoleBasedAccessControlFilterTest : public testing::Test {
public:
  RoleBasedAccessControlFilterConfigSharedPtr setupConfig() {
    envoy::config::rbac::v2alpha::Policy policy;
    policy.add_permissions()->set_destination_port(123);
    policy.add_principals()->set_any(true);
    envoy::config::filter::http::rbac::v2::RBAC config;
    config.mutable_rules()->set_action(envoy::config::rbac::v2alpha::RBAC::ALLOW);
    (*config.mutable_rules()->mutable_policies())["foo"] = policy;

    return std::make_shared<RoleBasedAccessControlFilterConfig>(config, "test", store_);
  }

  RoleBasedAccessControlFilterTest() : config_(setupConfig()), filter_(config_) {}

  void SetUp() {
    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  void setDestinationPort(uint16_t port, int times = 1) {
    address_ = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", port, false);
    auto& expect = EXPECT_CALL(connection_, localAddress());
    if (times > 0) {
      expect.Times(times);
    }
    expect.WillRepeatedly(ReturnRef(address_));
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Network::MockConnection> connection_{};
  Stats::IsolatedStoreImpl store_;
  RoleBasedAccessControlFilterConfigSharedPtr config_;
  RoleBasedAccessControlFilter filter_;
  Network::Address::InstanceConstSharedPtr address_;
  Http::TestHeaderMapImpl headers_;
};

TEST_F(RoleBasedAccessControlFilterTest, Allowed) {
  setDestinationPort(123);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(headers_));
}

TEST_F(RoleBasedAccessControlFilterTest, Denied) {
  setDestinationPort(456);

  Http::TestHeaderMapImpl response_headers{
      {":status", "403"},
      {"content-length", "19"},
      {"content-type", "text/plain"},
  };
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers_, true));
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

TEST_F(RoleBasedAccessControlFilterTest, RouteLocalOverride) {
  setDestinationPort(456, 0);

  NiceMock<Filters::Common::RBAC::MockEngine> engine;
  EXPECT_CALL(engine, allowed(_, _)).WillOnce(Return(true));
  EXPECT_CALL(callbacks_.route_->route_entry_, perFilterConfig(HttpFilterNames::get().RBAC))
      .WillRepeatedly(Return(&engine));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers_, true));
}

} // namespace
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
