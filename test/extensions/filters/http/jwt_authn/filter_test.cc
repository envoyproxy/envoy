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

class MockVerifier : public Verifier {
public:
  MOCK_METHOD2(verify, void(Http::HeaderMap& headers, Verifier::Callbacks& callback));
  MOCK_METHOD0(close, void());
};

class FilterTest : public ::testing::Test {
public:
  void SetUp() {
    config_ = ::std::make_shared<FilterConfig>(proto_config_, "", context_);

    mock_verifier_ = std::make_unique<MockVerifier>();
    raw_mock_verifier_ = static_cast<MockVerifier*>(mock_verifier_.get());

    std::vector<MatcherConstSharedPtr> matchers;
    auto mock_matcher_ = std::make_shared<NiceMock<MockMatcher>>();
    ON_CALL(*mock_matcher_.get(), matches(_)).WillByDefault(Invoke([](const Http::HeaderMap&) {
      return true;
    }));
    ON_CALL(*mock_matcher_.get(), verifier()).WillByDefault(Invoke([&]() -> const VerifierPtr& {
      return mock_verifier_;
    }));
    matchers.push_back(mock_matcher_);

    filter_ = std::make_unique<Filter>(config_->stats(), std::move(matchers));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  JwtAuthentication proto_config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  FilterConfigSharedPtr config_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  // MockMatcher* raw_mock_matcher_{};
  // NiceMock<MockMatcher> mock_matcher_;
  std::unique_ptr<Filter> filter_;
  VerifierPtr mock_verifier_;
  MockVerifier* raw_mock_verifier_;
};

// This test verifies Verifier::Callback is called inline with OK status.
// All functions should return Continue.
TEST_F(FilterTest, InlineOK) {
  // A successful authentication completed inline: callback is called inside verify().
  EXPECT_CALL(*raw_mock_verifier_, verify(_, _))
      .WillOnce(Invoke([](Http::HeaderMap&, Verifier::Callbacks& callback) {
        callback.onComplete(Status::Ok);
      }));

  auto headers = Http::TestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

// This test verifies Verifier::Callback is called inline with a failure status.
// All functions should return Continue except decodeHeaders(), it returns StopIteraton.
TEST_F(FilterTest, InlineFailure) {
  // A failed authentication completed inline: callback is called inside verify().
  EXPECT_CALL(*raw_mock_verifier_, verify(_, _))
      .WillOnce(Invoke([](Http::HeaderMap&, Verifier::Callbacks& callback) {
        callback.onComplete(Status::JwtBadFormat);
      }));

  auto headers = Http::TestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, config_->stats().denied_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

// This test verifies Verifier::Callback is called with OK status after verify().
TEST_F(FilterTest, OutBoundOK) {
  Verifier::Callbacks* m_cb;
  // callback is saved, not called right
  EXPECT_CALL(*raw_mock_verifier_, verify(_, _))
      .WillOnce(
          Invoke([&m_cb](Http::HeaderMap&, Verifier::Callbacks& callback) { m_cb = &callback; }));

  auto headers = Http::TestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(headers));

  // Callback is called now with OK status.
  m_cb->onComplete(Status::Ok);

  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

// This test verifies Verifier::Callback is called with a failure after verify().
TEST_F(FilterTest, OutBoundFailure) {
  Verifier::Callbacks* m_cb{};
  // callback is saved, not called right
  EXPECT_CALL(*raw_mock_verifier_, verify(_, _))
      .WillOnce(
          Invoke([&m_cb](Http::HeaderMap&, Verifier::Callbacks& callback) { m_cb = &callback; }));

  auto headers = Http::TestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(headers));

  // Callback is called now with a failure status.
  m_cb->onComplete(Status::JwtBadFormat);

  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));

  // Should be OK to call the onComplete() again.
  m_cb->onComplete(Status::JwtBadFormat);
}

// This test verifies Verifier::close() is called when Filter::onDestory() is called.
TEST_F(FilterTest, VerifyClose) {
  EXPECT_CALL(*raw_mock_verifier_, close()).Times(1);
  filter_->onDestroy();
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
