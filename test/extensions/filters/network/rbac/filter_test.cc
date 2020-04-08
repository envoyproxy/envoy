#include <memory>

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"

#include "common/network/utility.h"

#include "extensions/filters/common/rbac/utility.h"
#include "extensions/filters/network/rbac/rbac_filter.h"
#include "extensions/filters/network/well_known_names.h"

#include "test/mocks/network/mocks.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

class RoleBasedAccessControlNetworkFilterTest : public testing::Test {
public:
  RoleBasedAccessControlFilterConfigSharedPtr setupConfig(bool with_policy = true,
                                                          bool continuous = false) {
    envoy::extensions::filters::network::rbac::v3::RBAC config;
    config.set_stat_prefix("tcp.");

    if (with_policy) {
      envoy::config::rbac::v3::Policy policy;
      auto policy_rules = policy.add_permissions()->mutable_or_rules();
      policy_rules->add_rules()->mutable_requested_server_name()->set_hidden_envoy_deprecated_regex(
          ".*cncf.io");
      policy_rules->add_rules()->set_destination_port(123);
      policy.add_principals()->set_any(true);
      config.mutable_rules()->set_action(envoy::config::rbac::v3::RBAC::ALLOW);
      (*config.mutable_rules()->mutable_policies())["foo"] = policy;

      envoy::config::rbac::v3::Policy shadow_policy;
      auto shadow_policy_rules = shadow_policy.add_permissions()->mutable_or_rules();
      shadow_policy_rules->add_rules()->mutable_requested_server_name()->set_exact("xyz.cncf.io");
      shadow_policy_rules->add_rules()->set_destination_port(456);
      shadow_policy.add_principals()->set_any(true);
      config.mutable_shadow_rules()->set_action(envoy::config::rbac::v3::RBAC::ALLOW);
      (*config.mutable_shadow_rules()->mutable_policies())["bar"] = shadow_policy;
    }

    if (continuous) {
      config.set_enforcement_type(envoy::extensions::filters::network::rbac::v3::RBAC::CONTINUOUS);
    }

    return std::make_shared<RoleBasedAccessControlFilterConfig>(config, store_);
  }

  RoleBasedAccessControlNetworkFilterTest() : config_(setupConfig()) {
    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
    EXPECT_CALL(callbacks_.connection_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

    filter_ = std::make_unique<RoleBasedAccessControlFilter>(config_);
    filter_->initializeReadFilterCallbacks(callbacks_);
  }

  void setDestinationPort(uint16_t port) {
    address_ = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", port, false);
    EXPECT_CALL(stream_info_, downstreamLocalAddress()).WillRepeatedly(ReturnRef(address_));
  }

  void setRequestedServerName(std::string server_name) {
    requested_server_name_ = server_name;
    ON_CALL(callbacks_.connection_, requestedServerName())
        .WillByDefault(Return(requested_server_name_));
  }

  void setMetadata() {
    ON_CALL(stream_info_, setDynamicMetadata(NetworkFilterNames::get().Rbac, _))
        .WillByDefault(Invoke([this](const std::string&, const ProtobufWkt::Struct& obj) {
          stream_info_.metadata_.mutable_filter_metadata()->insert(
              Protobuf::MapPair<std::string, ProtobufWkt::Struct>(NetworkFilterNames::get().Rbac,
                                                                  obj));
        }));
  }

  NiceMock<Network::MockReadFilterCallbacks> callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  Stats::IsolatedStoreImpl store_;
  Buffer::OwnedImpl data_;
  RoleBasedAccessControlFilterConfigSharedPtr config_;

  std::unique_ptr<RoleBasedAccessControlFilter> filter_;
  Network::Address::InstanceConstSharedPtr address_;
  std::string requested_server_name_;
};

TEST_F(RoleBasedAccessControlNetworkFilterTest, AllowedWithOneTimeEnforcement) {
  setDestinationPort(123);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Call onData() twice, should only increase stats once.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, AllowedWithContinuousEnforcement) {
  config_ = setupConfig(true, true /* continuous enforcement */);
  filter_ = std::make_unique<RoleBasedAccessControlFilter>(config_);
  filter_->initializeReadFilterCallbacks(callbacks_);
  setDestinationPort(123);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Call onData() twice, should increase stats twice.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(2U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(2U, config_->stats().shadow_denied_.value());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, RequestedServerName) {
  setDestinationPort(999);
  setRequestedServerName("www.cncf.io");

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Call onData() twice, should only increase stats once.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, AllowedWithNoPolicy) {
  config_ = setupConfig(false /* with_policy */);
  filter_ = std::make_unique<RoleBasedAccessControlFilter>(config_);
  filter_->initializeReadFilterCallbacks(callbacks_);
  setDestinationPort(0);

  // Allow access and no metric change when there is no policy.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(0U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, Denied) {
  setDestinationPort(456);
  setMetadata();

  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush)).Times(2);

  // Call onData() twice, should only increase stats once.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  EXPECT_EQ(0U, config_->stats().allowed_.value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());

  auto filter_meta =
      stream_info_.dynamicMetadata().filter_metadata().at(NetworkFilterNames::get().Rbac);
  EXPECT_EQ("bar", filter_meta.fields().at("shadow_effective_policy_id").string_value());
  EXPECT_EQ("allowed", filter_meta.fields().at("shadow_engine_result").string_value());
}

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
