#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "extensions/filters/http/jwt_authn/filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::google::jwt_verify::Status;

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

class MockMatcher : public Matcher {
public:
  MOCK_METHOD(bool, matches, (const Http::RequestHeaderMap& headers), (const));
};

JwtAuthnFilterStats generateMockStats(Stats::Scope& scope) {
  return {ALL_JWT_AUTHN_FILTER_STATS(POOL_COUNTER_PREFIX(scope, ""))};
}

class MockFilterConfig : public FilterConfig {
public:
  MockFilterConfig() : stats_(generateMockStats(stats_store_)) {
    ON_CALL(*this, bypassCorsPreflightRequest()).WillByDefault(Return(true));
    ON_CALL(*this, findVerifier(_, _)).WillByDefault(Return(nullptr));
    ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
  }

  MOCK_METHOD(const Verifier*, findVerifier,
              (const Http::RequestHeaderMap& headers, const StreamInfo::FilterState& filter_state),
              (const));
  MOCK_METHOD(bool, bypassCorsPreflightRequest, (), (const));
  MOCK_METHOD(JwtAuthnFilterStats&, stats, ());

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  JwtAuthnFilterStats stats_;
};

class FilterTest : public testing::Test {
public:
  void SetUp() override {
    mock_config_ = ::std::make_shared<NiceMock<MockFilterConfig>>();

    mock_verifier_ = std::make_unique<MockVerifier>();
    filter_ = std::make_unique<Filter>(mock_config_);
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  void setupMockConfig() {
    EXPECT_CALL(*mock_config_.get(), findVerifier(_, _)).WillOnce(Return(mock_verifier_.get()));
  }

  std::shared_ptr<NiceMock<MockFilterConfig>> mock_config_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Filter> filter_;
  std::unique_ptr<MockVerifier> mock_verifier_;
  NiceMock<MockVerifierCallbacks> verifier_callback_;
  Http::TestRequestTrailerMapImpl trailers_;
};

// This test verifies Verifier::Callback is called inline with OK status.
// All functions should return Continue.
TEST_F(FilterTest, InlineOK) {
  setupMockConfig();
  // A successful authentication completed inline: callback is called inside verify().
  EXPECT_CALL(*mock_verifier_, verify(_)).WillOnce(Invoke([](ContextSharedPtr context) {
    context->callback()->onComplete(Status::Ok);
  }));

  auto headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(1U, mock_config_->stats().allowed_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));
}

// This test verifies Verifier::Callback is not called for CORS preflight request.
TEST_F(FilterTest, CorsPreflight) {
  auto headers = Http::TestRequestHeaderMapImpl{
      {":method", "OPTIONS"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"access-control-request-method", "GET"},
      {"origin", "test-origin"},
  };
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(1U, mock_config_->stats().allowed_.value());
  EXPECT_EQ(1U, mock_config_->stats().cors_preflight_bypassed_.value());
  EXPECT_EQ(0U, mock_config_->stats().denied_.value());
}

TEST_F(FilterTest, CorsPreflightMssingOrigin) {
  auto headers = Http::TestRequestHeaderMapImpl{
      {":method", "OPTIONS"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"access-control-request-method", "GET"},
  };
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, mock_config_->stats().allowed_.value());
  // Should not be bypassed by cors_preflight since missing origin.
  EXPECT_EQ(0U, mock_config_->stats().cors_preflight_bypassed_.value());
  EXPECT_EQ(0U, mock_config_->stats().denied_.value());
}

TEST_F(FilterTest, CorsPreflightMssingAccessControlRequestMethod) {
  auto headers = Http::TestRequestHeaderMapImpl{
      {":method", "OPTIONS"},    {":path", "/"}, {":scheme", "http"}, {":authority", "host"},
      {"origin", "test-origin"},
  };
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, mock_config_->stats().allowed_.value());
  // Should not be bypassed by cors_preflight since missing access-control-request-method.
  EXPECT_EQ(0U, mock_config_->stats().cors_preflight_bypassed_.value());
  EXPECT_EQ(0U, mock_config_->stats().denied_.value());
}

// This test verifies the setPayload call is handled correctly
TEST_F(FilterTest, TestSetPayloadCall) {
  setupMockConfig();
  ProtobufWkt::Struct payload;
  // A successful authentication completed inline: callback is called inside verify().
  EXPECT_CALL(*mock_verifier_, verify(_)).WillOnce(Invoke([&payload](ContextSharedPtr context) {
    context->callback()->setPayload(payload);
    context->callback()->onComplete(Status::Ok);
  }));

  EXPECT_CALL(filter_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([&payload](const std::string& ns, const ProtobufWkt::Struct& out_payload) {
        EXPECT_EQ(ns, HttpFilterNames::get().JwtAuthn);
        EXPECT_TRUE(TestUtility::protoEqual(out_payload, payload));
      }));

  auto headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, mock_config_->stats().allowed_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));
}

// This test verifies Verifier::Callback is called inline with a failure(401 Unauthorized) status.
// All functions should return Continue except decodeHeaders(), it returns StopIteration.
TEST_F(FilterTest, InlineUnauthorizedFailure) {
  setupMockConfig();
  // A failed authentication completed inline: callback is called inside verify().

  EXPECT_CALL(filter_callbacks_, sendLocalReply(Http::Code::Unauthorized, _, _, _, _));
  EXPECT_CALL(*mock_verifier_, verify(_)).WillOnce(Invoke([](ContextSharedPtr context) {
    context->callback()->onComplete(Status::JwtBadFormat);
  }));

  auto headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, mock_config_->stats().denied_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));
  EXPECT_EQ(filter_callbacks_.details(), "jwt_authn_access_denied{Jwt is not in the form of "
                                         "Header.Payload.Signature with two dots and 3 sections}");
}

// This test verifies Verifier::Callback is called inline with a failure(403 Forbidden) status.
// All functions should return Continue except decodeHeaders(), it returns StopIteration.
TEST_F(FilterTest, InlineForbiddenFailure) {
  setupMockConfig();
  // A failed authentication completed inline: callback is called inside verify().

  EXPECT_CALL(filter_callbacks_, sendLocalReply(Http::Code::Forbidden, _, _, _, _));
  EXPECT_CALL(*mock_verifier_, verify(_)).WillOnce(Invoke([](ContextSharedPtr context) {
    context->callback()->onComplete(Status::JwtAudienceNotAllowed);
  }));

  auto headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, mock_config_->stats().denied_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));
  EXPECT_EQ(filter_callbacks_.details(),
            "jwt_authn_access_denied{Audiences in Jwt are not allowed}");
}

// This test verifies Verifier::Callback is called with OK status after verify().
TEST_F(FilterTest, OutBoundOK) {
  setupMockConfig();
  Verifier::Callbacks* m_cb;
  // callback is saved, not called right
  EXPECT_CALL(*mock_verifier_, verify(_)).WillOnce(Invoke([&m_cb](ContextSharedPtr context) {
    m_cb = context->callback();
  }));

  auto headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(trailers_));

  // Callback is called now with OK status.
  m_cb->onComplete(Status::Ok);

  EXPECT_EQ(1U, mock_config_->stats().allowed_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));
}

// This test verifies Verifier::Callback is called with a failure(401 Unauthorized) after verify()
// returns any NonOK status except JwtAudienceNotAllowed.
TEST_F(FilterTest, OutBoundUnauthorizedFailure) {
  setupMockConfig();
  Verifier::Callbacks* m_cb;
  // callback is saved, not called right
  EXPECT_CALL(*mock_verifier_, verify(_)).WillOnce(Invoke([&m_cb](ContextSharedPtr context) {
    m_cb = context->callback();
  }));

  auto headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(trailers_));

  // Callback is called now with a failure status.
  EXPECT_CALL(filter_callbacks_, sendLocalReply(Http::Code::Unauthorized, _, _, _, _));
  m_cb->onComplete(Status::JwtBadFormat);

  EXPECT_EQ(1U, mock_config_->stats().denied_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));

  // Should be OK to call the onComplete() again.
  m_cb->onComplete(Status::JwtBadFormat);
}

// This test verifies Verifier::Callback is called with a failure(403 Forbidden) after verify()
// returns JwtAudienceNotAllowed.
TEST_F(FilterTest, OutBoundForbiddenFailure) {
  setupMockConfig();
  Verifier::Callbacks* m_cb;
  // callback is saved, not called right
  EXPECT_CALL(*mock_verifier_, verify(_)).WillOnce(Invoke([&m_cb](ContextSharedPtr context) {
    m_cb = context->callback();
  }));

  auto headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(trailers_));

  // Callback is called now with a failure status.
  EXPECT_CALL(filter_callbacks_, sendLocalReply(Http::Code::Forbidden, _, _, _, _));
  m_cb->onComplete(Status::JwtAudienceNotAllowed);

  EXPECT_EQ(1U, mock_config_->stats().denied_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));

  // Should be OK to call the onComplete() again.
  m_cb->onComplete(Status::JwtAudienceNotAllowed);
}

// Test verifies that if no route matched requirement, then request is allowed.
TEST_F(FilterTest, TestNoRouteMatched) {
  EXPECT_CALL(*mock_config_.get(), findVerifier(_, _)).WillOnce(Return(nullptr));

  auto headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, mock_config_->stats().allowed_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers_));
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
