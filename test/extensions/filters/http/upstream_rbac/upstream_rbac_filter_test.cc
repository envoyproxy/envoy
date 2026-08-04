#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/rbac/matchers/upstream_ip_port/v3/upstream_ip_port_matcher.pb.h"
#include "envoy/http/filter.h"

#include "source/common/network/utility.h"
#include "source/common/stream_info/upstream_address.h"
#include "source/extensions/filters/http/upstream_rbac/upstream_rbac_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace UpstreamRBACFilter {
namespace {

// Minimal fake implementation of UpstreamStreamFilterCallbacks that captures the registered
// UpstreamCallbacks so the test can drive onHostSelected() directly.
class FakeUpstreamStreamFilterCallbacks : public Http::UpstreamStreamFilterCallbacks {
public:
  StreamInfo::StreamInfo& upstreamStreamInfo() override { return stream_info_; }
  OptRef<Router::GenericUpstream> upstream() override { return {}; }
  void dumpState(std::ostream&, int) const override {}
  bool pausedForConnect() const override { return false; }
  void setPausedForConnect(bool) override {}
  bool pausedForWebsocketUpgrade() const override { return false; }
  void setPausedForWebsocketUpgrade(bool) override {}
  void disableRouteTimeoutForWebsocketUpgrade() override {}
  void disablePerTryTimeoutForWebsocketUpgrade() override {}
  const Http::ConnectionPool::Instance::StreamOptions& upstreamStreamOptions() const override {
    return options_;
  }
  void addUpstreamCallbacks(Http::UpstreamCallbacks& callbacks) override {
    registered_callbacks_ = &callbacks;
  }
  void setUpstreamToDownstream(Router::UpstreamToDownstream&) override {}

  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Http::ConnectionPool::Instance::StreamOptions options_{false, false};
  Http::UpstreamCallbacks* registered_callbacks_{};
};

class UpstreamRbacFilterTest : public testing::Test {
public:
  // Builds a single-policy RBAC config matching on the given upstream IP (prefix /32) and, if
  // require_header is set, additionally requiring the "x-block" request header to be present.
  void setup(envoy::config::rbac::v3::RBAC::Action action, const std::string& upstream_ip,
             bool require_header = false) {
    envoy::config::rbac::v3::Policy policy;
    auto* and_rules = policy.add_permissions()->mutable_and_rules();

    envoy::extensions::rbac::matchers::upstream_ip_port::v3::UpstreamIpPortMatcher matcher;
    matcher.mutable_upstream_ip()->set_address_prefix(upstream_ip);
    matcher.mutable_upstream_ip()->mutable_prefix_len()->set_value(32);
    auto* matcher_ext = and_rules->add_rules()->mutable_matcher();
    matcher_ext->set_name("envoy.rbac.matchers.upstream_ip_port");
    std::ignore = matcher_ext->mutable_typed_config()->PackFrom(matcher);

    if (require_header) {
      auto* header = and_rules->add_rules()->mutable_header();
      header->set_name("x-block");
      header->set_present_match(true);
    }

    policy.add_principals()->set_any(true);

    envoy::extensions::filters::http::rbac::v3::RBAC config;
    config.mutable_rules()->set_action(action);
    (*config.mutable_rules()->mutable_policies())["foo"] = policy;
    applyConfig(config);
  }

  // Builds the filter from an arbitrary RBAC config and wires the callbacks/mocks.
  void applyConfig(const envoy::extensions::filters::http::rbac::v3::RBAC& config) {
    config_ = std::make_shared<RBACFilter::RoleBasedAccessControlFilterConfig>(
        config, "test", *stats_store_.rootScope(), context_,
        ProtobufMessage::getStrictValidationVisitor());

    filter_ = std::make_unique<UpstreamRoleBasedAccessControlFilter>(config_);

    EXPECT_CALL(callbacks_, connection())
        .WillRepeatedly(Return(OptRef<const Network::Connection>{connection_}));
    EXPECT_CALL(callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
    EXPECT_CALL(callbacks_, upstreamCallbacks())
        .WillRepeatedly(Return(OptRef<Http::UpstreamStreamFilterCallbacks>{upstream_cbs_}));
    // The filter reads the request headers from the (downstream) stream info during
    // onHostSelected(), since its own decodeHeaders() has not run at that point.
    ON_CALL(req_info_, getRequestHeaders()).WillByDefault(Return(&headers_));
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  Upstream::HostDescriptionConstSharedPtr makeHost(const std::string& address) {
    auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
    ON_CALL(*host, address())
        .WillByDefault(
            Return(Network::Utility::parseInternetAddressAndPortNoThrow(address, false)));
    return host;
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> req_info_;
  FakeUpstreamStreamFilterCallbacks upstream_cbs_;
  Stats::TestUtil::TestStore stats_store_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  RBACFilter::RoleBasedAccessControlFilterConfigSharedPtr config_;
  std::unique_ptr<UpstreamRoleBasedAccessControlFilter> filter_;
  Http::TestRequestHeaderMapImpl headers_;
};

// The filter should register itself as an upstream callback during setDecoderFilterCallbacks.
TEST_F(UpstreamRbacFilterTest, RegistersUpstreamCallbacks) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");
  EXPECT_EQ(upstream_cbs_.registered_callbacks_, filter_.get());
}

// A DENY policy whose upstream_ip matches the selected host rejects the request before connecting.
TEST_F(UpstreamRbacFilterTest, DenyMatchingUpstreamIp) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::Forbidden, "RBAC: access denied", _, _, _));
  filter_->onHostSelected(makeHost("1.2.3.4:80"));

  EXPECT_EQ(1U, config_->stats().denied_.value());
  // The filter populates the upstream address filter state so the matcher can resolve it.
  EXPECT_TRUE(req_info_.filterState()->hasDataWithName(StreamInfo::UpstreamAddress::key()));
}

// A DENY policy whose upstream_ip does not match the selected host allows the request.
TEST_F(UpstreamRbacFilterTest, AllowNonMatchingUpstreamIp) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");
  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  filter_->onHostSelected(makeHost("5.6.7.8:80"));

  EXPECT_EQ(1U, config_->stats().allowed_.value());
}

// An ALLOW policy whose upstream_ip matches the selected host allows the request.
TEST_F(UpstreamRbacFilterTest, AllowMatchingUpstreamIp) {
  setup(envoy::config::rbac::v3::RBAC::ALLOW, "1.2.3.4");
  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  filter_->onHostSelected(makeHost("1.2.3.4:80"));

  EXPECT_EQ(1U, config_->stats().allowed_.value());
}

// Header matching is available at onHostSelected time: the request is denied only when both the
// upstream IP and the request header match.
TEST_F(UpstreamRbacFilterTest, CombinedHeaderAndUpstreamIpMatch) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4", /*require_header=*/true);

  // Header present + IP match -> denied.
  headers_.addCopy("x-block", "1");
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::Forbidden, _, _, _, _));
  filter_->onHostSelected(makeHost("1.2.3.4:80"));
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

TEST_F(UpstreamRbacFilterTest, HeaderAbsentIsAllowed) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4", /*require_header=*/true);

  // Header absent -> the AND permission does not match, so the request is allowed.
  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  filter_->onHostSelected(makeHost("1.2.3.4:80"));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
}

// A stale/foreign upstream address already in filter state (e.g. left by a previous retry attempt
// or by the dynamic forward proxy filter) must NOT be trusted: the filter overwrites it and
// evaluates the host selected for this attempt. Regression test for the deny-bypass-on-retry bug.
TEST_F(UpstreamRbacFilterTest, ReevaluatesSelectedHostIgnoringStaleFilterState) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");
  // Pre-populate the filter state with a different (allowed) address, as a prior attempt would.
  req_info_.filterState()->setData(
      StreamInfo::UpstreamAddress::key(),
      std::make_shared<StreamInfo::UpstreamAddress>(
          Network::Utility::parseInternetAddressAndPortNoThrow("5.6.7.8:80", false)),
      StreamInfo::FilterState::LifeSpan::Request);

  // The selected host matches the deny rule and must be denied, despite the stale allowed address.
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::Forbidden, _, _, _, _));
  filter_->onHostSelected(makeHost("1.2.3.4:80"));
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// The engine result is exposed as dynamic metadata under the upstream_rbac namespace (distinct from
// the downstream rbac filter's namespace).
TEST_F(UpstreamRbacFilterTest, EmitsDynamicMetadataUnderUpstreamRbacKey) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.filters.http.upstream_rbac", _));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _));
  filter_->onHostSelected(makeHost("1.2.3.4:80"));
}

// With no RBAC engine configured (empty config), the filter is a no-op: no denial, no metadata.
TEST_F(UpstreamRbacFilterTest, NoEngineConfiguredAllows) {
  applyConfig(envoy::extensions::filters::http::rbac::v3::RBAC{});
  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  filter_->onHostSelected(makeHost("1.2.3.4:80"));
}

// Per-route configuration works for the upstream filter exactly as for the downstream RBAC filter:
// it is inherited from RoleBasedAccessControlFilter, which resolves the route-specific config by
// the filter's config name over the (downstream) route. Here the main config denies the selected
// host but a route-local RBACPerRoute override allows it, and the override wins.
TEST_F(UpstreamRbacFilterTest, PerRouteConfigOverridesEngine) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");

  // Route-local config: allow everything.
  envoy::extensions::filters::http::rbac::v3::RBACPerRoute per_route;
  auto* rules = per_route.mutable_rbac()->mutable_rules();
  rules->set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  envoy::config::rbac::v3::Policy allow_all;
  allow_all.add_permissions()->set_any(true);
  allow_all.add_principals()->set_any(true);
  (*rules->mutable_policies())["allow-all"] = allow_all;
  RBACFilter::RoleBasedAccessControlRouteSpecificFilterConfig route_config(
      per_route, context_, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_CALL(callbacks_, mostSpecificPerFilterConfig()).WillRepeatedly(Return(&route_config));

  // The deny-matching host is allowed because the route-local engine takes precedence.
  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  filter_->onHostSelected(makeHost("1.2.3.4:80"));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
}

// A selected host with no resolved address (e.g. a logical host) is skipped without evaluating,
// crashing, or writing filter state.
TEST_F(UpstreamRbacFilterTest, NullHostAddressIsSkipped) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");
  auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  ON_CALL(*host, address()).WillByDefault(Return(nullptr));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  filter_->onHostSelected(host);
  EXPECT_FALSE(req_info_.filterState()->hasDataWithName(StreamInfo::UpstreamAddress::key()));
}

// With no downstream connection available at host-selection time, the filter records the selected
// address but skips evaluation rather than dereferencing an empty connection OptRef.
TEST_F(UpstreamRbacFilterTest, NoDownstreamConnectionIsSkipped) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(OptRef<const Network::Connection>{}));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  filter_->onHostSelected(makeHost("1.2.3.4:80"));

  // The address is written (that happens before the connection guard), but no policy is evaluated.
  EXPECT_TRUE(req_info_.filterState()->hasDataWithName(StreamInfo::UpstreamAddress::key()));
  EXPECT_EQ(0U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().allowed_.value());
}

// If the downstream stream info exposes no request headers at host-selection time, the filter
// evaluates against an empty header map rather than dereferencing null. The IP-only deny rule still
// matches and denies.
TEST_F(UpstreamRbacFilterTest, NullRequestHeadersUsesEmptyHeaders) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");
  EXPECT_CALL(req_info_, getRequestHeaders()).WillRepeatedly(Return(nullptr));

  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::Forbidden, _, _, _, _));
  filter_->onHostSelected(makeHost("1.2.3.4:80"));
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// decodeHeaders is a pass-through: the RBAC check runs in onHostSelected(), not during decode.
TEST_F(UpstreamRbacFilterTest, DecodeHeadersPassesThrough) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");
  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers_, false));
}

// onUpstreamConnectionEstablished is a no-op (the filter acts at host selection); exercise it to
// confirm it has no side effects.
TEST_F(UpstreamRbacFilterTest, OnUpstreamConnectionEstablishedIsNoOp) {
  setup(envoy::config::rbac::v3::RBAC::DENY, "1.2.3.4");
  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  filter_->onUpstreamConnectionEstablished();
}

} // namespace
} // namespace UpstreamRBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
