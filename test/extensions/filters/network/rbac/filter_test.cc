#include <memory>

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/common/rbac/utility.h"
#include "source/extensions/filters/network/rbac/rbac_filter.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "xds/type/matcher/v3/matcher.pb.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

class RoleBasedAccessControlNetworkFilterTest : public testing::Test {
public:
  void
  setupPolicy(bool with_policy = true, bool continuous = false,
              envoy::config::rbac::v3::RBAC::Action action = envoy::config::rbac::v3::RBAC::ALLOW,
              int64_t delay_deny_duration_ms = 0) {

    envoy::extensions::filters::network::rbac::v3::RBAC config;
    config.set_stat_prefix("tcp.");
    config.set_shadow_rules_stat_prefix("shadow_rules_prefix_");

    if (with_policy) {
      envoy::config::rbac::v3::Policy policy;
      auto policy_rules = policy.add_permissions()->mutable_or_rules();
      policy_rules->add_rules()->mutable_requested_server_name()->MergeFrom(
          TestUtility::createRegexMatcher(".*cncf.io"));
      policy_rules->add_rules()->set_destination_port(123);
      policy.add_principals()->set_any(true);
      config.mutable_rules()->set_action(action);
      (*config.mutable_rules()->mutable_policies())["foo"] = policy;

      envoy::config::rbac::v3::Policy shadow_policy;
      auto shadow_policy_rules = shadow_policy.add_permissions()->mutable_or_rules();
      shadow_policy_rules->add_rules()->mutable_requested_server_name()->set_exact("xyz.cncf.io");
      shadow_policy_rules->add_rules()->set_destination_port(456);
      shadow_policy.add_principals()->set_any(true);
      config.mutable_shadow_rules()->set_action(action);
      (*config.mutable_shadow_rules()->mutable_policies())["bar"] = shadow_policy;
    }

    if (continuous) {
      config.set_enforcement_type(envoy::extensions::filters::network::rbac::v3::RBAC::CONTINUOUS);
    }

    if (delay_deny_duration_ms > 0) {
      (*config.mutable_delay_deny()) =
          ProtobufUtil::TimeUtil::MillisecondsToDuration(delay_deny_duration_ms);
    }

    config_ = std::make_shared<RoleBasedAccessControlFilterConfig>(
        config, *store_.rootScope(), context_, ProtobufMessage::getStrictValidationVisitor());
    initFilter();
  }

  void setupMatcher(bool with_matcher = true, bool continuous = false, std::string action = "ALLOW",
                    std::string on_no_match_action = "DENY") {
    envoy::extensions::filters::network::rbac::v3::RBAC config;
    config.set_stat_prefix("tcp.");
    config.set_shadow_rules_stat_prefix("shadow_rules_prefix_");

    if (with_matcher) {
      constexpr absl::string_view matcher_yaml = R"EOF(
matcher_list:
  matchers:
  - predicate:
      or_matcher:
        predicate:
        - single_predicate:
            input:
              name: envoy.matching.inputs.server_name
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.ServerNameInput
            value_match:
              safe_regex:
                google_re2: {}
                regex: .*cncf.io
        - single_predicate:
            input:
              name: envoy.matching.inputs.destination_port
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationPortInput
            value_match:
              exact: "123"
    on_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: foo
          action: {}
on_no_match:
  action:
    name: action
    typed_config:
      "@type": type.googleapis.com/envoy.config.rbac.v3.Action
      name: none
      action: {}
)EOF";
      constexpr absl::string_view shadow_matcher_yaml = R"EOF(
matcher_list:
  matchers:
  - predicate:
      or_matcher:
        predicate:
        - single_predicate:
            input:
              name: envoy.matching.inputs.server_name
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.ServerNameInput
            value_match:
              exact: xyz.cncf.io
        - single_predicate:
            input:
              name: envoy.matching.inputs.destination_port
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationPortInput
            value_match:
              exact: "456"
    on_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: bar
          action: {}
on_no_match:
  action:
    name: action
    typed_config:
      "@type": type.googleapis.com/envoy.config.rbac.v3.Action
      name: none
      action: {}
)EOF";

      xds::type::matcher::v3::Matcher matcher;
      // Escape the first {} for safe_regex.
      TestUtility::loadFromYaml(fmt::format(matcher_yaml, "{}", action, on_no_match_action),
                                matcher);
      *config.mutable_matcher() = matcher;

      xds::type::matcher::v3::Matcher shadow_matcher;
      TestUtility::loadFromYaml(fmt::format(shadow_matcher_yaml, action, on_no_match_action),
                                shadow_matcher);
      *config.mutable_shadow_matcher() = shadow_matcher;
    }

    if (continuous) {
      config.set_enforcement_type(envoy::extensions::filters::network::rbac::v3::RBAC::CONTINUOUS);
    }

    config_ = std::make_shared<RoleBasedAccessControlFilterConfig>(
        config, *store_.rootScope(), context_, ProtobufMessage::getStrictValidationVisitor());
    initFilter();
  }

  void initFilter() {
    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
    EXPECT_CALL(callbacks_.connection_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

    filter_ = std::make_unique<RoleBasedAccessControlFilter>(config_);
    filter_->initializeReadFilterCallbacks(callbacks_);
  }

  void setDestinationPort(uint16_t port) {
    address_ = Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", port, false);

    stream_info_.downstream_connection_info_provider_->setLocalAddress(address_);
    ON_CALL(callbacks_.connection_.stream_info_, downstreamAddressProvider())
        .WillByDefault(ReturnPointee(stream_info_.downstream_connection_info_provider_));
  }

  void setRequestedServerName(std::string server_name) {
    requested_server_name_ = server_name;
    ON_CALL(callbacks_.connection_, requestedServerName())
        .WillByDefault(Return(requested_server_name_));

    stream_info_.downstream_connection_info_provider_->setRequestedServerName(
        requested_server_name_);
    ON_CALL(callbacks_.connection_.stream_info_, downstreamAddressProvider())
        .WillByDefault(ReturnPointee(stream_info_.downstream_connection_info_provider_));
  }

  void checkAccessLogMetadata(bool expected) {
    auto filter_meta = stream_info_.dynamicMetadata().filter_metadata().at(
        Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace);
    EXPECT_EQ(expected,
              filter_meta.fields()
                  .at(Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().AccessLogKey)
                  .bool_value());
  }

  void setMetadata() {
    ON_CALL(stream_info_, setDynamicMetadata(NetworkFilterNames::get().Rbac, _))
        .WillByDefault(Invoke([this](const std::string&, const ProtobufWkt::Struct& obj) {
          stream_info_.metadata_.mutable_filter_metadata()->insert(
              Protobuf::MapPair<std::string, ProtobufWkt::Struct>(NetworkFilterNames::get().Rbac,
                                                                  obj));
        }));

    ON_CALL(stream_info_,
            setDynamicMetadata(
                Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace, _))
        .WillByDefault(Invoke([this](const std::string&, const ProtobufWkt::Struct& obj) {
          stream_info_.metadata_.mutable_filter_metadata()->insert(
              Protobuf::MapPair<std::string, ProtobufWkt::Struct>(
                  Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace, obj));
        }));
  }

  NiceMock<Network::MockReadFilterCallbacks> callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  Stats::IsolatedStoreImpl store_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Buffer::OwnedImpl data_;
  RoleBasedAccessControlFilterConfigSharedPtr config_;

  std::unique_ptr<RoleBasedAccessControlFilter> filter_;
  Network::Address::InstanceConstSharedPtr address_;
  std::string requested_server_name_;
};

TEST_F(RoleBasedAccessControlNetworkFilterTest, AllowedWithOneTimeEnforcement) {
  setupPolicy();

  setDestinationPort(123);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Call onData() twice, should only increase stats once.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, AllowedWithContinuousEnforcement) {
  setupPolicy(true, true /* continuous enforcement */);

  setDestinationPort(123);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Call onData() twice, should increase stats twice.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(2U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(2U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, RequestedServerName) {
  setupPolicy();

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
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, AllowedWithNoPolicy) {
  setupPolicy(false /* with_policy */);

  setDestinationPort(0);

  // Allow access and no metric change when there is no policy.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(0U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, Denied) {
  setupPolicy();

  setDestinationPort(456);
  setMetadata();

  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _)).Times(2);

  // Call onData() twice, should only increase stats once.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  EXPECT_EQ(0U, config_->stats().allowed_.value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  auto filter_meta =
      stream_info_.dynamicMetadata().filter_metadata().at(NetworkFilterNames::get().Rbac);
  EXPECT_EQ(
      "bar",
      filter_meta.fields().at("shadow_rules_prefix_shadow_effective_policy_id").string_value());
  EXPECT_EQ("allowed",
            filter_meta.fields().at("shadow_rules_prefix_shadow_engine_result").string_value());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, DelayDenied) {
  int64_t delay_deny_duration_ms = 500;
  setupPolicy(true, false, envoy::config::rbac::v3::RBAC::ALLOW, delay_deny_duration_ms);
  setDestinationPort(789);

  // Only call close() once since the connection is delay denied.
  EXPECT_CALL(callbacks_.connection_, readDisable(true));
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));

  Event::MockTimer* delay_timer =
      new NiceMock<Event::MockTimer>(&callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*delay_timer, enableTimer(std::chrono::milliseconds(delay_deny_duration_ms), _));

  // Call onData() twice, should only increase stats once.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  EXPECT_EQ(0U, config_->stats().allowed_.value());
  EXPECT_EQ(1U, config_->stats().denied_.value());

  delay_timer->invokeCallback();
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, MatcherAllowedWithOneTimeEnforcement) {
  setupMatcher();

  setDestinationPort(123);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Call onData() twice, should only increase stats once.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, MatcherAllowedWithContinuousEnforcement) {
  setupMatcher(true, true /* continuous enforcement */);

  setDestinationPort(123);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Call onData() twice, should increase stats twice.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(2U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(2U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, RequestedServerNameMatcher) {
  setupMatcher();

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
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, AllowedWithNoMatcher) {
  setupMatcher(false);

  setDestinationPort(0);

  // Allow access and no metric change when there is no policy.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(0U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, MatcherDenied) {
  setupMatcher();

  setDestinationPort(456);
  setMetadata();

  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _)).Times(2);

  // Call onData() twice, should only increase stats once.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  EXPECT_EQ(0U, config_->stats().allowed_.value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  auto filter_meta =
      stream_info_.dynamicMetadata().filter_metadata().at(NetworkFilterNames::get().Rbac);
  EXPECT_EQ(
      "bar",
      filter_meta.fields().at("shadow_rules_prefix_shadow_effective_policy_id").string_value());
  EXPECT_EQ("allowed",
            filter_meta.fields().at("shadow_rules_prefix_shadow_engine_result").string_value());
}

// Log Tests
TEST_F(RoleBasedAccessControlNetworkFilterTest, ShouldLog) {
  setupPolicy(true, false, envoy::config::rbac::v3::RBAC::LOG);

  setDestinationPort(123);
  setMetadata();

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  checkAccessLogMetadata(true);
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, ShouldNotLog) {
  setupPolicy(true, false, envoy::config::rbac::v3::RBAC::LOG);

  setDestinationPort(456);
  setMetadata();

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  checkAccessLogMetadata(false);
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, AllowNoChangeLog) {
  setupPolicy();

  setDestinationPort(123);
  setMetadata();

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));

  // Check that Allow action does not set access log metadata
  EXPECT_EQ(stream_info_.dynamicMetadata().filter_metadata().end(),
            stream_info_.dynamicMetadata().filter_metadata().find(
                Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace));
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, MatcherShouldLog) {
  setupMatcher(true, false, "LOG", "ALLOW");

  setDestinationPort(123);
  setMetadata();

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  checkAccessLogMetadata(true);
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, MatcherShouldNotLog) {
  setupMatcher(true, false, "LOG", "ALLOW");

  setDestinationPort(456);
  setMetadata();

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_allowed",
            config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.shadow_rules_prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  checkAccessLogMetadata(false);
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, MatcherAllowNoChangeLog) {
  setupMatcher();

  setDestinationPort(123);
  setMetadata();

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));

  // Check that Allow action does not set access log metadata
  EXPECT_EQ(stream_info_.dynamicMetadata().filter_metadata().end(),
            stream_info_.dynamicMetadata().filter_metadata().find(
                Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace));
}

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
