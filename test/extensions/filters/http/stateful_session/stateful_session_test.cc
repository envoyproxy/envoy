#include <memory>

#include "source/extensions/filters/http/stateful_session/stateful_session.h"
#include "source/server/generic_factory_context.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stateful_session.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {
namespace {

class StatefulSessionTest : public testing::Test {
public:
  void initialize(absl::string_view config, absl::string_view route_config = "") {
    Http::MockSessionStateFactoryConfig config_factory;
    Registry::InjectFactory<Http::SessionStateFactoryConfig> registration(config_factory);

    factory_ = std::make_shared<NiceMock<Http::MockSessionStateFactory>>();
    EXPECT_CALL(config_factory, createSessionStateFactory(_, _)).WillOnce(Return(factory_));

    ASSERT(!config.empty());
    ProtoConfig proto_config;
    TestUtility::loadFromYaml(std::string(config), proto_config);
    Envoy::Server::GenericFactoryContextImpl generic_context(context_);

    config_ = std::make_shared<StatefulSessionConfig>(proto_config, generic_context);

    filter_ = std::make_shared<StatefulSession>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);

    if (!route_config.empty()) {
      PerRouteProtoConfig proto_route_config;
      TestUtility::loadFromYaml(std::string(route_config), proto_route_config);

      if (proto_route_config.has_stateful_session()) {
        route_factory_ = std::make_shared<NiceMock<Http::MockSessionStateFactory>>();
        EXPECT_CALL(config_factory, createSessionStateFactory(_, _))
            .WillOnce(Return(route_factory_));
      }

      route_config_ =
          std::make_shared<PerRouteStatefulSession>(proto_route_config, generic_context);

      ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
          .WillByDefault(Return(route_config_.get()));
    }
  };

  NiceMock<Server::Configuration::MockFactoryContext> context_;

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;

  std::shared_ptr<NiceMock<Http::MockSessionStateFactory>> factory_;
  std::shared_ptr<NiceMock<Http::MockSessionStateFactory>> route_factory_;

  StatefulSessionConfigSharedPtr config_;
  PerRouteStatefulSessionConfigSharedPtr route_config_;

  std::shared_ptr<StatefulSession> filter_;
};

constexpr absl::string_view ConfigYaml = R"EOF(
session_state:
  name: "envoy.http.stateful_session.mock"
  typed_config: {}
)EOF";

constexpr absl::string_view DisableYaml = R"EOF(
disabled: true
)EOF";

constexpr absl::string_view RouteConfigYaml = R"EOF(
stateful_session:
  session_state:
    name: "envoy.http.stateful_session.mock"
    typed_config: {}
)EOF";

// Test the normal case that the stateful session is enabled.
TEST_F(StatefulSessionTest, NormalSessionStateTest) {
  initialize(ConfigYaml);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {":authority", "test.com"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  auto session_state = std::make_unique<NiceMock<Http::MockSessionState>>();
  auto raw_session_state = session_state.get();

  EXPECT_CALL(*factory_, create(_)).WillOnce(Return(testing::ByMove(std::move(session_state))));
  EXPECT_CALL(*raw_session_state, upstreamAddress())
      .WillOnce(Return(absl::make_optional<absl::string_view>("1.2.3.4")));
  EXPECT_CALL(decoder_callbacks_, setUpstreamOverrideHost(_))
      .WillOnce(testing::Invoke([&](Upstream::LoadBalancerContext::OverrideHost host) {
        EXPECT_EQ("1.2.3.4", host.first);
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(*raw_session_state, onUpdate(_, _));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

// Test the case that the stateful session is disabled by the route config.
TEST_F(StatefulSessionTest, SessionStateDisabledByRoute) {
  initialize(ConfigYaml, DisableYaml);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {":authority", "test.com"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  EXPECT_CALL(*factory_, create(_)).Times(0);

  EXPECT_EQ(nullptr, filter_->sessionStateForTest().get());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

// Test the case that the stateful session is override by the route config.
TEST_F(StatefulSessionTest, SessionStateOverrideByRoute) {
  initialize(ConfigYaml, RouteConfigYaml);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {":authority", "test.com"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  auto session_state = std::make_unique<NiceMock<Http::MockSessionState>>();
  auto raw_session_state = session_state.get();

  EXPECT_CALL(*route_factory_, create(_))
      .WillOnce(Return(testing::ByMove(std::move(session_state))));
  EXPECT_CALL(*raw_session_state, upstreamAddress())
      .WillOnce(Return(absl::make_optional<absl::string_view>("1.2.3.4")));
  EXPECT_CALL(decoder_callbacks_, setUpstreamOverrideHost(_))
      .WillOnce(testing::Invoke([&](Upstream::LoadBalancerContext::OverrideHost host) {
        EXPECT_EQ("1.2.3.4", host.first);
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(*raw_session_state, onUpdate(_, _));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

// Test the case that the session state has not valid upstream address.
TEST_F(StatefulSessionTest, SessionStateHasNoUpstreamAddress) {
  initialize(ConfigYaml, RouteConfigYaml);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {":authority", "test.com"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  auto session_state = std::make_unique<NiceMock<Http::MockSessionState>>();
  auto raw_session_state = session_state.get();

  EXPECT_CALL(*route_factory_, create(_))
      .WillOnce(Return(testing::ByMove(std::move(session_state))));
  EXPECT_CALL(*raw_session_state, upstreamAddress()).WillOnce(Return(absl::nullopt));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(*raw_session_state, onUpdate(_, _));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

// Test the case that no valid upstream host.
TEST_F(StatefulSessionTest, NoUpstreamHost) {
  initialize(ConfigYaml);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {":authority", "test.com"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  auto session_state = std::make_unique<NiceMock<Http::MockSessionState>>();
  auto raw_session_state = session_state.get();

  EXPECT_CALL(*factory_, create(_)).WillOnce(Return(testing::ByMove(std::move(session_state))));
  EXPECT_CALL(*raw_session_state, upstreamAddress())
      .WillOnce(Return(absl::make_optional<absl::string_view>("1.2.3.4")));
  EXPECT_CALL(decoder_callbacks_, setUpstreamOverrideHost(_))
      .WillOnce(testing::Invoke([&](Upstream::LoadBalancerContext::OverrideHost host) {
        EXPECT_EQ("1.2.3.4", host.first);
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  encoder_callbacks_.stream_info_.setUpstreamInfo(nullptr);
  EXPECT_CALL(*raw_session_state, onUpdate(_, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

// Test the case that no valid session state.
TEST_F(StatefulSessionTest, NullSessionState) {
  initialize(ConfigYaml);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {":authority", "test.com"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  EXPECT_CALL(*factory_, create(_)).WillOnce(Return(testing::ByMove(nullptr)));
  EXPECT_CALL(decoder_callbacks_, setUpstreamOverrideHost(_)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

TEST(EmpytProtoConfigTest, EmpytProtoConfigTest) {
  ProtoConfig empty_proto_config;
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> generic_context;

  StatefulSessionConfig config(empty_proto_config, generic_context);

  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {":authority", "test.com"}};
  EXPECT_EQ(nullptr, config.createSessionState(request_headers));
}

} // namespace
} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
