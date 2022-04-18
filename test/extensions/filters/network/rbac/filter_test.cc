#include <memory>

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/common/rbac/utility.h"
#include "source/extensions/filters/network/rbac/rbac_filter.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "xds/type/matcher/v3/matcher.pb.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

class RoleBasedAccessControlNetworkFilterTest : public testing::Test {
public:
  void
  setupPolicy(bool with_policy = true, bool continuous = false,
              envoy::config::rbac::v3::RBAC::Action action = envoy::config::rbac::v3::RBAC::ALLOW) {

    envoy::extensions::filters::network::rbac::v3::RBAC config;
    config.set_stat_prefix("tcp.");
    config.set_shadow_rules_stat_prefix("prefix_");

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

    config_ = std::make_shared<RoleBasedAccessControlFilterConfig>(
        config, store_, context_, ProtobufMessage::getStrictValidationVisitor());

    filter_ = std::make_unique<RoleBasedAccessControlFilter>(config_);
    filter_->initializeReadFilterCallbacks(callbacks_);
  }

  void setupMatcher(
      bool with_matcher = true, bool continuous = false,
      envoy::config::rbac::v3::RBAC::Action rbac_action = envoy::config::rbac::v3::RBAC::ALLOW,
      envoy::config::rbac::v3::RBAC::Action on_no_match_rbac_action =
          envoy::config::rbac::v3::RBAC::DENY) {
    envoy::extensions::filters::network::rbac::v3::RBAC config;
    config.set_stat_prefix("tcp.");
    config.set_shadow_rules_stat_prefix("prefix_");

    if (with_matcher) {
      envoy::extensions::matching::common_inputs::network::v3::ServerNameInput server_name_input;
      envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput
          destination_port_input;
      envoy::config::rbac::v3::Action action;
      action.set_name("foo");
      action.set_action(rbac_action);
      envoy::config::rbac::v3::Action shadow_action;
      shadow_action.set_name("bar");
      shadow_action.set_action(rbac_action);
      envoy::config::rbac::v3::Action on_no_match_action;
      on_no_match_action.set_name("none");
      on_no_match_action.set_action(on_no_match_rbac_action);

      xds::type::matcher::v3::Matcher matcher;
      {
        auto matcher_matcher = matcher.mutable_matcher_list()->mutable_matchers()->Add();
        auto matcher_action = matcher_matcher->mutable_on_match()->mutable_action();
        matcher_action->set_name("action");
        matcher_action->mutable_typed_config()->PackFrom(action);
        auto matcher_or_matcher = matcher_matcher->mutable_predicate()->mutable_or_matcher();
        auto matcher_server_name_predicate =
            matcher_or_matcher->add_predicate()->mutable_single_predicate();
        auto matcher_server_name_input = matcher_server_name_predicate->mutable_input();
        matcher_server_name_input->set_name("envoy.matching.inputs.server_name");
        matcher_server_name_input->mutable_typed_config()->PackFrom(server_name_input);
        auto matcher_regex =
            matcher_server_name_predicate->mutable_value_match()->mutable_safe_regex();
        matcher_regex->mutable_google_re2();
        matcher_regex->set_regex(".*cncf.io");
        auto matcher_destination_port_predicate =
            matcher_or_matcher->add_predicate()->mutable_single_predicate();
        auto matcher_destination_port_input = matcher_destination_port_predicate->mutable_input();
        matcher_destination_port_input->set_name("envoy.matching.inputs.destination_port");
        matcher_destination_port_input->mutable_typed_config()->PackFrom(destination_port_input);
        matcher_destination_port_predicate->mutable_value_match()->set_exact("123");

        auto matcher_on_no_match_action = matcher.mutable_on_no_match()->mutable_action();
        matcher_on_no_match_action->set_name("action");
        matcher_on_no_match_action->mutable_typed_config()->PackFrom(on_no_match_action);
      }
      *config.mutable_matcher() = matcher;

      xds::type::matcher::v3::Matcher shadow_matcher;
      {
        auto matcher_matcher = shadow_matcher.mutable_matcher_list()->mutable_matchers()->Add();
        auto matcher_action = matcher_matcher->mutable_on_match()->mutable_action();
        matcher_action->set_name("action");
        matcher_action->mutable_typed_config()->PackFrom(shadow_action);
        auto matcher_or_matcher = matcher_matcher->mutable_predicate()->mutable_or_matcher();
        auto matcher_server_name_predicate =
            matcher_or_matcher->add_predicate()->mutable_single_predicate();
        auto matcher_server_name_input = matcher_server_name_predicate->mutable_input();
        matcher_server_name_input->set_name("envoy.matching.inputs.server_name");
        matcher_server_name_input->mutable_typed_config()->PackFrom(server_name_input);
        matcher_server_name_predicate->mutable_value_match()->set_exact("xyz.cncf.io");
        auto matcher_destination_port_predicate =
            matcher_or_matcher->add_predicate()->mutable_single_predicate();
        auto matcher_destination_port_input = matcher_destination_port_predicate->mutable_input();
        matcher_destination_port_input->set_name("envoy.matching.inputs.destination_port");
        matcher_destination_port_input->mutable_typed_config()->PackFrom(destination_port_input);
        matcher_destination_port_predicate->mutable_value_match()->set_exact("456");

        auto matcher_on_no_match_action = shadow_matcher.mutable_on_no_match()->mutable_action();
        matcher_on_no_match_action->set_name("action");
        matcher_on_no_match_action->mutable_typed_config()->PackFrom(on_no_match_action);
      }
      *config.mutable_shadow_matcher() = shadow_matcher;
    }

    if (continuous) {
      config.set_enforcement_type(envoy::extensions::filters::network::rbac::v3::RBAC::CONTINUOUS);
    }

    config_ = std::make_shared<RoleBasedAccessControlFilterConfig>(
        config, store_, context_, ProtobufMessage::getStrictValidationVisitor());

    filter_ = std::make_unique<RoleBasedAccessControlFilter>(config_);
    filter_->initializeReadFilterCallbacks(callbacks_);
  }

  RoleBasedAccessControlNetworkFilterTest()
      : provider_(std::make_shared<Network::Address::Ipv4Instance>(80),
                  std::make_shared<Network::Address::Ipv4Instance>(80)) {
    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
    EXPECT_CALL(callbacks_.connection_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

    filter_ = std::make_unique<RoleBasedAccessControlFilter>(config_);
    filter_->initializeReadFilterCallbacks(callbacks_);
  }

  void setDestinationPort(uint16_t port) {
    address_ = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", port, false);
    stream_info_.downstream_connection_info_provider_->setLocalAddress(address_);

    provider_.setLocalAddress(address_);
    ON_CALL(callbacks_.connection_, connectionInfoProvider()).WillByDefault(ReturnRef(provider_));
  }

  void setRequestedServerName(std::string server_name) {
    requested_server_name_ = server_name;
    ON_CALL(callbacks_.connection_, requestedServerName())
        .WillByDefault(Return(requested_server_name_));

    provider_.setRequestedServerName(requested_server_name_);
    ON_CALL(callbacks_.connection_, connectionInfoProvider()).WillByDefault(ReturnRef(provider_));
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
  Network::ConnectionInfoSetterImpl provider_;
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
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());
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
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());
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
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());
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
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, Denied) {
  setupPolicy();

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
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  auto filter_meta =
      stream_info_.dynamicMetadata().filter_metadata().at(NetworkFilterNames::get().Rbac);
  EXPECT_EQ("bar", filter_meta.fields().at("prefix_shadow_effective_policy_id").string_value());
  EXPECT_EQ("allowed", filter_meta.fields().at("prefix_shadow_engine_result").string_value());
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
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());
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
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());
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
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());
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
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, MatcherDenied) {
  setupMatcher();

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
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  auto filter_meta =
      stream_info_.dynamicMetadata().filter_metadata().at(NetworkFilterNames::get().Rbac);
  EXPECT_EQ("bar", filter_meta.fields().at("prefix_shadow_effective_policy_id").string_value());
  EXPECT_EQ("allowed", filter_meta.fields().at("prefix_shadow_engine_result").string_value());
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
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

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
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

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
  setupMatcher(true, false, envoy::config::rbac::v3::RBAC::LOG,
               envoy::config::rbac::v3::RBAC::ALLOW);

  setDestinationPort(123);
  setMetadata();

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  checkAccessLogMetadata(true);
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, MatcherShouldNotLog) {
  setupMatcher(true, false, envoy::config::rbac::v3::RBAC::LOG,
               envoy::config::rbac::v3::RBAC::ALLOW);

  setDestinationPort(456);
  setMetadata();

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("tcp.rbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("tcp.rbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("tcp.rbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

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
