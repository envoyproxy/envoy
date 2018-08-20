#include "common/config/metadata.h"
#include "common/network/utility.h"

#include "extensions/filters/common/rbac/utility.h"
#include "extensions/filters/http/rbac/rbac_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/extensions/filters/common/rbac/mocks.h"
#include "test/extensions/filters/http/rbac/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {
namespace {

class RoleBasedAccessControlFilterTest : public testing::Test {
public:
  RoleBasedAccessControlFilterConfigSharedPtr setupConfig() {
    envoy::config::filter::http::rbac::v2::RBAC config;

    envoy::config::rbac::v2alpha::Policy policy;
    policy.add_permissions()->set_destination_port(123);
    policy.add_principals()->set_any(true);
    config.mutable_rules()->set_action(envoy::config::rbac::v2alpha::RBAC::ALLOW);
    (*config.mutable_rules()->mutable_policies())["foo"] = policy;

    envoy::config::rbac::v2alpha::Policy shadow_policy;
    shadow_policy.add_permissions()->set_destination_port(456);
    shadow_policy.add_principals()->set_any(true);
    config.mutable_shadow_rules()->set_action(envoy::config::rbac::v2alpha::RBAC::ALLOW);
    (*config.mutable_shadow_rules()->mutable_policies())["bar"] = shadow_policy;

    return std::make_shared<RoleBasedAccessControlFilterConfig>(config, "test", store_);
  }

  RoleBasedAccessControlFilterTest() : config_(setupConfig()), filter_(config_) {}

  void SetUp() {
    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
    EXPECT_CALL(callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  void setDestinationPort(uint16_t port) {
    address_ = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", port, false);
    ON_CALL(connection_, localAddress()).WillByDefault(ReturnRef(address_));
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Network::MockConnection> connection_{};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> req_info_;
  Stats::IsolatedStoreImpl store_;
  RoleBasedAccessControlFilterConfigSharedPtr config_;

  envoy::api::v2::core::Metadata metadata_;
  RoleBasedAccessControlFilter filter_;
  Network::Address::InstanceConstSharedPtr address_;
  Http::TestHeaderMapImpl headers_;
};

TEST_F(RoleBasedAccessControlFilterTest, Allowed) {
  setDestinationPort(123);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());

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
  EXPECT_EQ(1U, config_->stats().shadow_allowed_.value());
}

TEST_F(RoleBasedAccessControlFilterTest, RouteLocalOverride) {
  setDestinationPort(456);

  envoy::config::filter::http::rbac::v2::RBACPerRoute route_config;
  route_config.mutable_rbac()->mutable_rules()->set_action(
      envoy::config::rbac::v2alpha::RBAC_Action::RBAC_Action_DENY);
  NiceMock<Filters::Common::RBAC::MockEngine> engine{route_config.rbac().rules()};
  NiceMock<MockRoleBasedAccessControlRouteSpecificFilterConfig> per_route_config_{route_config};

  EXPECT_CALL(engine, allowed(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(per_route_config_, engine()).WillRepeatedly(ReturnRef(engine));

  EXPECT_CALL(callbacks_.route_->route_entry_, perFilterConfig(HttpFilterNames::get().Rbac))
      .WillRepeatedly(Return(&per_route_config_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers_, true));
}

} // namespace
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
