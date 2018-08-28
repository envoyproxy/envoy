#include "extensions/filters/http/jwt_authn/filter.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::google::jwt_verify::Status;

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class MockMatcher : public Matcher {
public:
  MOCK_CONST_METHOD1(matches, bool(const Http::HeaderMap& headers));
  MOCK_CONST_METHOD0(verifier, const VerifierPtr&());
};

class MockVerifierCallbacks : public VerifierCallbacks {
public:
  MOCK_METHOD2(onComplete, void(const Status& status, VerifyContext& context));
};

class MockVerifier : public Verifier {
public:
  MOCK_METHOD1(verify, void(VerifyContext& context));
  MOCK_METHOD1(registerCallback, void(VerifierCallbacks* callback));
};

class MockFilterConfig : public FilterConfig {
public:
  MockFilterConfig(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context)
      : FilterConfig(proto_config, stats_prefix, context) {}
  MOCK_CONST_METHOD1(findVerifier, const MatcherConstSharedPtr(const Http::HeaderMap& headers));
};

class FilterTest : public ::testing::Test {
public:
  void SetUp() {
    mock_config_ = ::std::make_shared<MockFilterConfig>(proto_config_, "", mock_context_);

    mock_verifier_ = std::make_unique<MockVerifier>();
    raw_mock_verifier_ = static_cast<MockVerifier*>(mock_verifier_.get());

    EXPECT_CALL(*mock_config_.get(), findVerifier(_)).WillOnce(Invoke([&](const Http::HeaderMap&) {
      auto mock_matcher = std::make_shared<NiceMock<MockMatcher>>();
      ON_CALL(*mock_matcher.get(), matches(_)).WillByDefault(Invoke([](const Http::HeaderMap&) {
        return true;
      }));
      ON_CALL(*mock_matcher.get(), verifier()).WillByDefault(Invoke([&]() -> const VerifierPtr& {
        return mock_verifier_;
      }));
      return mock_matcher;
    }));

    filter_ = std::make_unique<Filter>(mock_config_);
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  JwtAuthentication proto_config_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_context_;
  std::shared_ptr<MockFilterConfig> mock_config_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Filter> filter_;
  VerifierPtr mock_verifier_;
  MockVerifier* raw_mock_verifier_;
  NiceMock<MockVerifierCallbacks> verifier_callback_;
};

// This test verifies Verifier::Callback is called inline with OK status.
// All functions should return Continue.
TEST_F(FilterTest, InlineOK) {
  // A successful authentication completed inline: callback is called inside verify().
  EXPECT_CALL(*raw_mock_verifier_, verify(_)).WillOnce(Invoke([](VerifyContext& context) {
    context.callback()->onComplete(Status::Ok, context);
  }));

  auto headers = Http::TestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, mock_config_->stats().allowed_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

// This test verifies Verifier::Callback is called inline with a failure status.
// All functions should return Continue except decodeHeaders(), it returns StopIteraton.
TEST_F(FilterTest, InlineFailure) {
  // A failed authentication completed inline: callback is called inside verify().
  EXPECT_CALL(*raw_mock_verifier_, verify(_)).WillOnce(Invoke([](VerifyContext& context) {
    context.callback()->onComplete(Status::JwtBadFormat, context);
  }));

  auto headers = Http::TestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, mock_config_->stats().denied_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

// This test verifies Verifier::Callback is called with OK status after verify().
TEST_F(FilterTest, OutBoundOK) {
  VerifierCallbacks* m_cb;
  // callback is saved, not called right
  EXPECT_CALL(*raw_mock_verifier_, verify(_)).WillOnce(Invoke([&m_cb](VerifyContext& context) {
    m_cb = context.callback();
  }));

  auto headers = Http::TestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(headers));

  // Callback is called now with OK status.
  auto context = VerifyContext::create(headers, &verifier_callback_);
  m_cb->onComplete(Status::Ok, *context);

  EXPECT_EQ(1U, mock_config_->stats().allowed_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

// This test verifies Verifier::Callback is called with a failure after verify().
TEST_F(FilterTest, OutBoundFailure) {
  VerifierCallbacks* m_cb;
  // callback is saved, not called right
  EXPECT_CALL(*raw_mock_verifier_, verify(_)).WillOnce(Invoke([&m_cb](VerifyContext& context) {
    m_cb = context.callback();
  }));

  auto headers = Http::TestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(headers));

  auto context = VerifyContext::create(headers, &verifier_callback_);
  // Callback is called now with a failure status.
  m_cb->onComplete(Status::JwtBadFormat, *context);

  EXPECT_EQ(1U, mock_config_->stats().denied_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));

  // Should be OK to call the onComplete() again.
  m_cb->onComplete(Status::JwtBadFormat, *context);
}

// This test verifies Verifier::cancel() is called when Filter::onDestory() is called.
TEST_F(FilterTest, VerifyCancel) {
  EXPECT_CALL(*raw_mock_verifier_, verify(_)).WillOnce(Invoke([](VerifyContext& context) {
    auto auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*auth.get(), onDestroy()).Times(1);
    context.addAuth(std::move(auth));
  }));
  auto headers = Http::TestHeaderMapImpl{};
  filter_->decodeHeaders(headers, false);
  filter_->onDestroy();
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
