#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/extensions/rbac/matchers/upstream_ip_port/v3/upstream_ip_port_matcher.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"

#include "source/common/config/metadata.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/stream_info/upstream_address.h"
#include "source/extensions/filters/common/rbac/utility.h"
#include "source/extensions/filters/http/rbac/rbac_filter.h"

#include "test/extensions/filters/common/rbac/mocks.h"
#include "test/extensions/filters/http/rbac/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "xds/type/matcher/v3/matcher.pb.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {
namespace {

enum class LogResult { Yes, No, Undecided };

class RoleBasedAccessControlFilterTest : public testing::Test {
public:
  void setupPolicy(envoy::config::rbac::v3::RBAC::Action action) {
    envoy::extensions::filters::http::rbac::v3::RBAC config;

    envoy::config::rbac::v3::Policy policy;
    auto policy_rules = policy.add_permissions()->mutable_or_rules();
    policy_rules->add_rules()->mutable_requested_server_name()->MergeFrom(
        TestUtility::createRegexMatcher(".*cncf.io"));
    policy_rules->add_rules()->set_destination_port(123);
    policy_rules->add_rules()->mutable_url_path()->mutable_path()->set_suffix("suffix");
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
    config.set_shadow_rules_stat_prefix("prefix_");

    setupConfig(std::make_shared<RoleBasedAccessControlFilterConfig>(
        config, "test", store_, context_, ProtobufMessage::getStrictValidationVisitor()));
  }

  void setupMatcher(std::string action, std::string on_no_match_action) {
    envoy::extensions::filters::http::rbac::v3::RBAC config;

    const std::string matcher_yaml = R"EOF(
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
        - single_predicate:
            input:
              name: envoy.matching.inputs.request_headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: ":path"
            value_match:
              # TODO(zhxie): it is not equivalent with the URL path rule in setupPolicy(). There
              # will be a replacement when the URL path custom matcher is ready.
              prefix: "/suffix"
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
    const std::string shadow_matcher_yaml = R"EOF(
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
    TestUtility::loadFromYaml(fmt::format(matcher_yaml, "{}", action, on_no_match_action), matcher);
    *config.mutable_matcher() = matcher;

    xds::type::matcher::v3::Matcher shadow_matcher;
    TestUtility::loadFromYaml(fmt::format(shadow_matcher_yaml, action, on_no_match_action),
                              shadow_matcher);
    *config.mutable_shadow_matcher() = shadow_matcher;
    config.set_shadow_rules_stat_prefix("prefix_");

    setupConfig(std::make_shared<RoleBasedAccessControlFilterConfig>(
        config, "test", store_, context_, ProtobufMessage::getStrictValidationVisitor()));
  }

  void setupConfig(RoleBasedAccessControlFilterConfigSharedPtr config) {
    config_ = config;
    filter_ = std::make_unique<RoleBasedAccessControlFilter>(config_);

    EXPECT_CALL(callbacks_, connection())
        .WillRepeatedly(Return(OptRef<const Network::Connection>{connection_}));
    EXPECT_CALL(callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  RoleBasedAccessControlFilterTest()
      : provider_(std::make_shared<Network::Address::Ipv4Instance>(80),
                  std::make_shared<Network::Address::Ipv4Instance>(80)){};

  void setDestinationPort(uint16_t port) {
    address_ = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", port, false);
    req_info_.downstream_connection_info_provider_->setLocalAddress(address_);

    provider_.setLocalAddress(address_);
    ON_CALL(connection_, connectionInfoProvider()).WillByDefault(ReturnRef(provider_));
  }

  void setRequestedServerName(std::string server_name) {
    requested_server_name_ = server_name;
    ON_CALL(connection_, requestedServerName()).WillByDefault(Return(requested_server_name_));

    provider_.setRequestedServerName(server_name);
    ON_CALL(connection_, connectionInfoProvider()).WillByDefault(ReturnRef(provider_));
  }

  void checkAccessLogMetadata(LogResult expected) {
    if (expected != LogResult::Undecided) {
      auto filter_meta = req_info_.dynamicMetadata().filter_metadata().at(
          Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace);
      EXPECT_EQ(expected == LogResult::Yes,
                filter_meta.fields()
                    .at(Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().AccessLogKey)
                    .bool_value());
    } else {
      EXPECT_EQ(req_info_.dynamicMetadata().filter_metadata().end(),
                req_info_.dynamicMetadata().filter_metadata().find(
                    Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace));
    }
  }

  void setMetadata() {
    ON_CALL(req_info_, setDynamicMetadata("envoy.filters.http.rbac", _))
        .WillByDefault(Invoke([this](const std::string&, const ProtobufWkt::Struct& obj) {
          req_info_.metadata_.mutable_filter_metadata()->insert(
              Protobuf::MapPair<std::string, ProtobufWkt::Struct>("envoy.filters.http.rbac", obj));
        }));

    ON_CALL(req_info_,
            setDynamicMetadata(
                Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace, _))
        .WillByDefault(Invoke([this](const std::string&, const ProtobufWkt::Struct& obj) {
          req_info_.metadata_.mutable_filter_metadata()->insert(
              Protobuf::MapPair<std::string, ProtobufWkt::Struct>(
                  Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace, obj));
        }));
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Network::MockConnection> connection_{};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info_;
  Stats::IsolatedStoreImpl store_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  RoleBasedAccessControlFilterConfigSharedPtr config_;
  std::unique_ptr<RoleBasedAccessControlFilter> filter_;

  Network::Address::InstanceConstSharedPtr address_;
  Network::ConnectionInfoSetterImpl provider_;
  std::string requested_server_name_;
  Http::TestRequestHeaderMapImpl headers_;
  Http::TestRequestTrailerMapImpl trailers_;
};

TEST_F(RoleBasedAccessControlFilterTest, Allowed) {
  setupPolicy(envoy::config::rbac::v3::RBAC::ALLOW);

  setDestinationPort(123);
  setMetadata();

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("testrbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("testrbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));

  checkAccessLogMetadata(LogResult::Undecided);
}

TEST_F(RoleBasedAccessControlFilterTest, RequestedServerName) {
  setupPolicy(envoy::config::rbac::v3::RBAC::ALLOW);

  setDestinationPort(999);
  setRequestedServerName("www.cncf.io");
  setMetadata();

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("testrbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("testrbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));

  checkAccessLogMetadata(LogResult::Undecided);
}

TEST_F(RoleBasedAccessControlFilterTest, Path) {
  setupPolicy(envoy::config::rbac::v3::RBAC::ALLOW);

  setDestinationPort(999);
  setMetadata();

  auto headers = Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/suffix#seg?param=value"},
      {":scheme", "http"},
      {":authority", "host"},
  };
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  checkAccessLogMetadata(LogResult::Undecided);
}

TEST_F(RoleBasedAccessControlFilterTest, Denied) {
  setupPolicy(envoy::config::rbac::v3::RBAC::ALLOW);

  setDestinationPort(456);
  setMetadata();

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "403"},
      {"content-length", "19"},
      {"content-type", "text/plain"},
  };
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers_, true));
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ("testrbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("testrbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  auto filter_meta = req_info_.dynamicMetadata().filter_metadata().at("envoy.filters.http.rbac");
  EXPECT_EQ("allowed", filter_meta.fields().at("prefix_shadow_engine_result").string_value());
  EXPECT_EQ("bar", filter_meta.fields().at("prefix_shadow_effective_policy_id").string_value());
  EXPECT_EQ("rbac_access_denied_matched_policy[none]", callbacks_.details());
  checkAccessLogMetadata(LogResult::Undecided);
}

TEST_F(RoleBasedAccessControlFilterTest, RouteLocalOverride) {
  setupPolicy(envoy::config::rbac::v3::RBAC::ALLOW);

  setDestinationPort(456);
  setMetadata();

  envoy::extensions::filters::http::rbac::v3::RBACPerRoute route_config;
  route_config.mutable_rbac()->mutable_rules()->set_action(envoy::config::rbac::v3::RBAC::DENY);
  NiceMock<Filters::Common::RBAC::MockEngine> engine{route_config.rbac().rules()};
  NiceMock<MockRoleBasedAccessControlRouteSpecificFilterConfig> per_route_config_{route_config,
                                                                                  context_};

  EXPECT_CALL(engine, handleAction(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(per_route_config_, engine()).WillRepeatedly(ReturnRef(engine));

  EXPECT_CALL(*callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillRepeatedly(Return(&per_route_config_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, true));
  checkAccessLogMetadata(LogResult::Undecided);
}

TEST_F(RoleBasedAccessControlFilterTest, MatcherAllowed) {
  setupMatcher("ALLOW", "DENY");

  setDestinationPort(123);
  setMetadata();

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("testrbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("testrbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));

  checkAccessLogMetadata(LogResult::Undecided);
}

TEST_F(RoleBasedAccessControlFilterTest, RequestedServerNameMatcher) {
  setupMatcher("ALLOW", "DENY");

  setDestinationPort(999);
  setRequestedServerName("www.cncf.io");
  setMetadata();

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("testrbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("testrbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));

  checkAccessLogMetadata(LogResult::Undecided);
}

TEST_F(RoleBasedAccessControlFilterTest, PathMatcher) {
  setupMatcher("ALLOW", "DENY");

  setDestinationPort(999);
  setMetadata();

  auto headers = Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/suffix#seg?param=value"},
      {":scheme", "http"},
      {":authority", "host"},
  };
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  checkAccessLogMetadata(LogResult::Undecided);
}

TEST_F(RoleBasedAccessControlFilterTest, MatcherDenied) {
  setupMatcher("ALLOW", "DENY");

  setDestinationPort(456);
  setMetadata();

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "403"},
      {"content-length", "19"},
      {"content-type", "text/plain"},
  };
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers_, true));
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, config_->stats().shadow_allowed_.value());
  EXPECT_EQ("testrbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("testrbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  auto filter_meta = req_info_.dynamicMetadata().filter_metadata().at("envoy.filters.http.rbac");
  EXPECT_EQ("allowed", filter_meta.fields().at("prefix_shadow_engine_result").string_value());
  EXPECT_EQ("bar", filter_meta.fields().at("prefix_shadow_effective_policy_id").string_value());
  EXPECT_EQ("rbac_access_denied_matched_policy[none]", callbacks_.details());
  checkAccessLogMetadata(LogResult::Undecided);
}

TEST_F(RoleBasedAccessControlFilterTest, MatcherRouteLocalOverride) {
  setupMatcher("ALLOW", "DENY");

  setDestinationPort(456);
  setMetadata();

  envoy::extensions::filters::http::rbac::v3::RBACPerRoute route_config;
  envoy::config::rbac::v3::Action action;
  action.set_name("none");
  action.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  xds::type::matcher::v3::Matcher matcher;
  auto matcher_on_no_match_action = matcher.mutable_on_no_match()->mutable_action();
  matcher_on_no_match_action->set_name("action");
  matcher_on_no_match_action->mutable_typed_config()->PackFrom(action);
  *route_config.mutable_rbac()->mutable_matcher() = matcher;
  ActionValidationVisitor validation_visitor;
  NiceMock<Filters::Common::RBAC::MockMatcherEngine> engine{route_config.rbac().matcher(), context_,
                                                            validation_visitor};
  NiceMock<MockRoleBasedAccessControlRouteSpecificFilterConfig> per_route_config_{route_config,
                                                                                  context_};

  EXPECT_CALL(engine, handleAction(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(per_route_config_, engine()).WillRepeatedly(ReturnRef(engine));

  EXPECT_CALL(*callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillRepeatedly(Return(&per_route_config_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, true));
  checkAccessLogMetadata(LogResult::Undecided);
}

// Log Tests
TEST_F(RoleBasedAccessControlFilterTest, ShouldLog) {
  setupPolicy(envoy::config::rbac::v3::RBAC::LOG);

  setDestinationPort(123);
  setMetadata();

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("testrbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("testrbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));

  checkAccessLogMetadata(LogResult::Yes);
}

TEST_F(RoleBasedAccessControlFilterTest, ShouldNotLog) {
  setupPolicy(envoy::config::rbac::v3::RBAC::LOG);

  setDestinationPort(456);
  setMetadata();

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("testrbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("testrbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));

  checkAccessLogMetadata(LogResult::No);
}

TEST_F(RoleBasedAccessControlFilterTest, MatcherShouldLog) {
  setupMatcher("LOG", "ALLOW");

  setDestinationPort(123);
  setMetadata();

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("testrbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("testrbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));

  checkAccessLogMetadata(LogResult::Yes);
}

TEST_F(RoleBasedAccessControlFilterTest, MatcherShouldNotLog) {
  setupMatcher("LOG", "ALLOW");

  setDestinationPort(456);
  setMetadata();

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ("testrbac.allowed", config_->stats().allowed_.name());
  EXPECT_EQ("testrbac.denied", config_->stats().denied_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_allowed", config_->stats().shadow_allowed_.name());
  EXPECT_EQ("testrbac.prefix_.shadow_denied", config_->stats().shadow_denied_.name());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));

  checkAccessLogMetadata(LogResult::No);
}

// Upstream Ip and Port matcher tests.
class UpstreamIpPortMatcherTests : public RoleBasedAccessControlFilterTest {
public:
  struct UpstreamIpPortMatcherConfig {
    UpstreamIpPortMatcherConfig() = default;

    UpstreamIpPortMatcherConfig(const std::string& ip) : ip_(ip) {}

    UpstreamIpPortMatcherConfig(uint16_t start, uint16_t end) {
      envoy::type::v3::Int64Range port_range;
      port_range.set_start(start);
      port_range.set_end(end);
      port_range_ = port_range;
    }

    UpstreamIpPortMatcherConfig(const std::string& ip, uint16_t start, uint16_t end) : ip_(ip) {
      envoy::type::v3::Int64Range port_range;
      port_range.set_start(start);
      port_range.set_end(end);
      port_range_ = port_range;
    }

    absl::optional<std::string> ip_;
    absl::optional<envoy::type::v3::Int64Range> port_range_;
  };

  void upstreamIpTestsBasicPolicySetup(const std::vector<UpstreamIpPortMatcherConfig>& configs,
                                       const envoy::config::rbac::v3::RBAC::Action& action) {
    envoy::config::rbac::v3::Policy policy;

    auto policy_rules = policy.add_permissions()->mutable_or_rules();
    policy_rules->add_rules()->mutable_requested_server_name()->MergeFrom(
        TestUtility::createRegexMatcher(".*cncf.io"));

    for (const auto& config : configs) {
      envoy::extensions::rbac::matchers::upstream_ip_port::v3::UpstreamIpPortMatcher matcher;

      if (config.ip_) {
        matcher.mutable_upstream_ip()->set_address_prefix(*config.ip_);
        matcher.mutable_upstream_ip()->mutable_prefix_len()->set_value(32);
      }

      if (config.port_range_) {
        *matcher.mutable_upstream_port_range() = config.port_range_.value();
      }

      auto* matcher_ext_config = policy_rules->add_rules()->mutable_matcher();

      *matcher_ext_config->mutable_name() = "envoy.rbac.matchers.upstream.upstream_ip_port";

      matcher_ext_config->mutable_typed_config()->PackFrom(matcher);
    }

    policy.add_principals()->set_any(true);

    envoy::extensions::filters::http::rbac::v3::RBAC config;
    config.mutable_rules()->set_action(action);
    (*config.mutable_rules()->mutable_policies())["foo"] = policy;

    auto config_ptr = std::make_shared<RoleBasedAccessControlFilterConfig>(
        config, "test", store_, context_, ProtobufMessage::getStrictValidationVisitor());

    // Setup test with the policy config.
    setupConfig(config_ptr);
  }

  void upstreamIpTestsFilterStateSetup(NiceMock<Http::MockStreamDecoderFilterCallbacks>& callback,
                                       const std::vector<std::string>& upstream_ips) {
    auto address_obj = std::make_unique<StreamInfo::UpstreamAddress>();

    for (const auto& ip : upstream_ips) {
      Network::Address::InstanceConstSharedPtr address =
          Envoy::Network::Utility::parseInternetAddressAndPort(ip, false);

      address_obj->address_ = address;
    }

    // Set the filter state data.
    callback.streamInfo().filterState()->setData(
        StreamInfo::UpstreamAddress::key(), std::move(address_obj),
        StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Request);
  }
};

// Tests simple permission policy with no upstream ip metadata in the filter state.
TEST_F(UpstreamIpPortMatcherTests, UpstreamIpNoFilterStateMetadata) {
  const std::vector<UpstreamIpPortMatcherConfig> configs = {
      {"1.2.3.4"},
  };
  // Setup policy config.
  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::ALLOW);

  // Filter iteration should be stopped as there is no filter state metadata.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers_, false));

  // Expect `denied` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Tests simple upstream_ip ALLOW permission policy with ONLY upstream ip metadata in the filter
// state.
TEST_F(UpstreamIpPortMatcherTests, UpstreamIpWithFilterStateAllow) {
  // Setup policy config.
  const std::vector<UpstreamIpPortMatcherConfig> configs = {
      {"1.2.3.4"},
  };
  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::ALLOW);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"1.2.3.4:123"});

  // Filter iteration should continue since the policy is ALLOW.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));

  // Expect `allowed` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().allowed_.value());
}

// Tests simple upstream_ip DENY permission policy with ONLY upstream ip metadata in the filter
// state.
TEST_F(UpstreamIpPortMatcherTests, UpstreamIpWithFilterStateDeny) {
  // Setup policy config.
  const std::vector<UpstreamIpPortMatcherConfig> configs = {
      {"1.2.3.4"},
  };

  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::DENY);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"1.2.3.4:123"});

  // Filter iteration should stop since the policy is DENY.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers_, false));

  // Expect `denied` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Tests simple upstream_ip DENY permission policy with BOTH upstream ip and port matching the
// policy.
TEST_F(UpstreamIpPortMatcherTests, UpstreamIpPortMatchDeny) {
  const std::vector<UpstreamIpPortMatcherConfig> configs = {
      {"1.2.3.4", 120, 123},
  };

  // Setup policy config.
  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::DENY);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"1.2.3.4:123"});

  // Filter iteration should stop since the policy is DENY.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers_, false));

  // Expect `denied` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

TEST_F(UpstreamIpPortMatcherTests, UpstreamIpPortMatchAllow) {
  const std::vector<UpstreamIpPortMatcherConfig> configs = {
      {"1.2.3.4", 120, 123},
  };

  // Setup policy config.
  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::ALLOW);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"1.2.3.4:123"});

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));

  // Expect `allowed` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().allowed_.value());
}

TEST_F(UpstreamIpPortMatcherTests, UpstreamPortMatchDeny) {
  const std::vector<UpstreamIpPortMatcherConfig> configs = {
      {120, 123},
  };

  // Setup policy config.
  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::DENY);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"1.2.3.4:123"});

  // Filter iteration should stop since the policy is DENY.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers_, false));

  // Expect `denied` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

TEST_F(UpstreamIpPortMatcherTests, UpstreamPortMatchAllow) {
  const std::vector<UpstreamIpPortMatcherConfig> configs = {
      {120, 123},
  };

  // Setup policy config.
  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::ALLOW);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"1.2.3.4:123"});

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));

  // Expect `allowed` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().allowed_.value());
}

// Tests upstream_ip DENY permission policy with multiple upstream ips to match in the policy.
// If any of the configured upstream ip addresses match the metadata, the policy is enforced (DENY).
TEST_F(UpstreamIpPortMatcherTests, MultiUpstreamIpsAnyPolicyDeny) {
  // Setup policy config.
  const std::vector<UpstreamIpPortMatcherConfig> configs = {
      {"1.1.1.2"}, {"1.2.3.4", 120, 123}, {"1.2.3.5"}};

  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::DENY);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"1.2.3.4:123"});

  // Filter iteration should stop since the policy is DENY.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers_, false));

  // Expect `denied` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Tests upstream_ip DENY permission policy with multiple upstream ips to match in the policy.
// If ONLY port is configured in the policy, a match should enforce the policy.
TEST_F(UpstreamIpPortMatcherTests, MultiUpstreamIpsNoIpMatchPortMatchDeny) {
  // Setup policy config.
  const std::vector<UpstreamIpPortMatcherConfig> configs = {{"1.1.1.2"}, {120, 123}, {"1.2.3.5"}};

  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::DENY);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"2.2.3.4:123"});

  // Filter iteration should stop since the policy is DENY.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers_, false));

  // Expect `denied` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Tests upstream_ip DENY permission policy with multiple upstream ips to match in the policy.
// If NONE of the configured upstream ip addresses or port match the metadata, the policy is NOT
// enforced.
TEST_F(UpstreamIpPortMatcherTests, MultiUpstreamIpsNoIpMatchNoPortMatchDeny) {
  // Setup policy config.
  const std::vector<UpstreamIpPortMatcherConfig> configs = {{"1.1.1.2"}, {124, 125}, {"1.2.3.5"}};

  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::DENY);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"2.2.3.4:123"});

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));

  // Expect `allowed` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().allowed_.value());
}

// Tests upstream_ip DENY permission policy with multiple upstream ips to match in the policy.
// If NONE of the configured upstream ip addresses or port match the metadata, the policy is NOT
// enforced.
TEST_F(UpstreamIpPortMatcherTests, MultiUpstreamIpsAnyPolicyNoMatchDeny) {
  // Setup policy config.
  const std::vector<UpstreamIpPortMatcherConfig> configs = {
      {"1.1.1.2"}, {"1.2.3.4", 124, 125}, {"1.2.3.5"}};

  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::DENY);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"1.2.3.4:123"});

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));

  // Expect `allowed` stats to be incremented.
  EXPECT_EQ(1U, config_->stats().allowed_.value());
}

// Tests simple DENY permission policy with misconfigured port range.
TEST_F(UpstreamIpPortMatcherTests, UpstreamPortBadRangeDeny) {
  const std::vector<UpstreamIpPortMatcherConfig> configs = {
      {8080, 0},
  };

  // Setup policy config.
  upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::DENY);

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"1.2.3.4:8080"});

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));

  EXPECT_EQ(0, config_->stats().denied_.value());
}

// Verifies that if no IP or port is configured, EnvoyException is thrown.
TEST_F(UpstreamIpPortMatcherTests, EmptyUpstreamConfigPolicyDeny) {
  // Setup policy config.
  const std::vector<UpstreamIpPortMatcherConfig> configs = {{}};

  // Setup filter state with the upstream address.
  upstreamIpTestsFilterStateSetup(callbacks_, {"1.2.3.4:123"});

  EXPECT_THROW_WITH_MESSAGE(
      upstreamIpTestsBasicPolicySetup(configs, envoy::config::rbac::v3::RBAC::DENY), EnvoyException,
      "Invalid UpstreamIpPortMatcher configuration - missing `upstream_ip` "
      "and/or `upstream_port_range`");
}

} // namespace
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
