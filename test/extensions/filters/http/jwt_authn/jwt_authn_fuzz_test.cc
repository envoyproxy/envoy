#include <memory>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.validate.h"

#include "source/common/common/regex.h"
#include "source/common/http/message_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/jwt_authn/filter.h"
#include "source/extensions/filters/http/jwt_authn/filter_config.h"

#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/jwt_authn/jwt_authn_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

using envoy::extensions::filters::http::jwt_authn::JwtAuthnFuzzInput;
using envoy::extensions::filters::http::jwt_authn::v3::PerRouteConfig;
using testing::NiceMock;
using testing::Return;

class MockJwksUpstream {
public:
  MockJwksUpstream(Upstream::MockClusterManager& mock_cm, const std::string& remote_jwks)
      : remote_jwks_(remote_jwks), jwks_request_(&mock_cm.thread_local_cluster_.async_client_) {
    ON_CALL(mock_cm, getThreadLocalCluster(_)).WillByDefault(Return(&thread_local_cluster_));
    ON_CALL(thread_local_cluster_.async_client_, send_(_, _, _))
        .WillByDefault(
            Invoke([this](const Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callback,
                          const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              if (remote_jwks_.empty()) {
                callback.onFailure(jwks_request_, Http::AsyncClient::FailureReason::Reset);
              } else {
                Http::ResponseMessagePtr response_message(
                    new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                        new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
                response_message->body().add(remote_jwks_);
                callback.onSuccess(jwks_request_, std::move(response_message));
              }
              return &jwks_request_;
            }));
  }

private:
  std::string remote_jwks_;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  NiceMock<Http::MockAsyncClientRequest> jwks_request_;
};

class MockPerRouteConfig {
public:
  MockPerRouteConfig(Http::MockStreamDecoderFilterCallbacks& mock_callback,
                     const PerRouteConfig& per_route) {
    per_route_config_ = std::make_shared<PerRouteFilterConfig>(per_route);
    mock_route_ = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
    ON_CALL(mock_callback, route()).WillByDefault(Return(mock_route_));
    ON_CALL(*mock_route_, mostSpecificPerFilterConfig(_))
        .WillByDefault(Return(per_route_config_.get()));
  }

private:
  std::shared_ptr<NiceMock<Envoy::Router::MockRoute>> mock_route_;
  std::shared_ptr<PerRouteFilterConfig> per_route_config_;
};

DEFINE_PROTO_FUZZER(const JwtAuthnFuzzInput& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks;

  // Test the expired token.
  // The jwt token in the corpus files expired at 2001001001 (at year 2033).
  if (input.force_jwt_expired()) {
    // 20 years == 615168000 seconds.
    mock_factory_ctx.server_factory_context_.time_system_.advanceTimeWait(
        Envoy::Seconds(615168000));
  }

  MockJwksUpstream mock_jwks(mock_factory_ctx.server_factory_context_.cluster_manager_,
                             input.remote_jwks());

  // Mock per route config.
  std::unique_ptr<MockPerRouteConfig> mock_per_route;
  if (input.has_per_route()) {
    mock_per_route = std::make_unique<MockPerRouteConfig>(filter_callbacks, input.per_route());
  }

  // Set filter_state_selector only there is a valid filter_state_rules.
  // Otherwise filter_state take over, and there is not filter_state_rules, it
  // just bail out.
  if (input.config().has_filter_state_rules()) {
    const auto& rules = input.config().filter_state_rules();
    if (!rules.name().empty() && !rules.requires_().empty()) {
      filter_callbacks.stream_info_.filter_state_->setData(
          rules.name(), std::make_unique<Router::StringAccessorImpl>(input.filter_state_selector()),
          StreamInfo::FilterState::StateType::ReadOnly,
          StreamInfo::FilterState::LifeSpan::FilterChain);
    }
  }

  std::shared_ptr<FilterConfigImpl> filter_config;
  try {
    filter_config = std::make_shared<FilterConfigImpl>(input.config(), "", mock_factory_ctx);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during filter config construction: {}", e.what());
    return;
  }

  // Simulate multiple calls to execute jwt_cache and jwks_cache codes
  for (uint32_t i = 0; i < input.num_calls(); i++) {
    auto filter = std::make_unique<Filter>(filter_config);
    filter->setDecoderFilterCallbacks(filter_callbacks);

    HttpFilterFuzzer fuzzer;
    fuzzer.runData(static_cast<Http::StreamDecoderFilter*>(filter.get()), input.request_data());

    if (input.filter_on_destroy()) {
      filter->onDestroy();
    }

    fuzzer.reset();
  }
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
