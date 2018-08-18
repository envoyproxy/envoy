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

class MockAsyncMatcher : public AsyncMatcher {
public:
  MOCK_METHOD2(matches, void(Http::HeaderMap& headers, AsyncMatcher::Callbacks& callback));
  MOCK_METHOD0(close, void());
};

class FilterTest : public ::testing::Test {
public:
  void SetUp() {
    config_ = ::std::make_shared<FilterConfig>(proto_config_, "", context_);
    std::vector<AsyncMatcherSharedPtr> matchers;
    auto mock_matcher = std::make_unique<MockAsyncMatcher>();
    matchers.push_back(std::move(mock_matcher));
    raw_mock_matcher_ = static_cast<MockAsyncMatcher*>(matchers.back().get());
    filter_ = std::make_unique<Filter>(config_->stats(), std::move(matchers));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  JwtAuthentication proto_config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  FilterConfigSharedPtr config_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  MockAsyncMatcher* raw_mock_matcher_{};
  std::unique_ptr<Filter> filter_;
};

// This test verifies AsyncMatcher::Callback is called inline with OK status.
// All functions should return Continue.
TEST_F(FilterTest, InlineOK) {
  // A successful authentication completed inline: callback is called inside verify().
  EXPECT_CALL(*raw_mock_matcher_, matches(_, _))
      .WillOnce(Invoke([](Http::HeaderMap&, AsyncMatcher::Callbacks& callback) {
        callback.onComplete(Status::Ok);
      }));

  auto headers = Http::TestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

// This test verifies AsyncMatcher::Callback is called inline with a failure status.
// All functions should return Continue except decodeHeaders(), it returns StopIteraton.
TEST_F(FilterTest, InlineFailure) {
  // A failed authentication completed inline: callback is called inside verify().
  EXPECT_CALL(*raw_mock_matcher_, matches(_, _))
      .WillOnce(Invoke([](Http::HeaderMap&, AsyncMatcher::Callbacks& callback) {
        callback.onComplete(Status::JwtBadFormat);
      }));

  auto headers = Http::TestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, config_->stats().denied_.value());

  Buffer::OwnedImpl data("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

// This test verifies AsyncMatcher::Callback is called with OK status after verify().
TEST_F(FilterTest, OutBoundOK) {
  AsyncMatcher::Callbacks* m_cb;
  // callback is saved, not called right
  EXPECT_CALL(*raw_mock_matcher_, matches(_, _))
      .WillOnce(Invoke(
          [&m_cb](Http::HeaderMap&, AsyncMatcher::Callbacks& callback) { m_cb = &callback; }));

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

// This test verifies AsyncMatcher::Callback is called with a failure after verify().
TEST_F(FilterTest, OutBoundFailure) {
  AsyncMatcher::Callbacks* m_cb{};
  // callback is saved, not called right
  EXPECT_CALL(*raw_mock_matcher_, matches(_, _))
      .WillOnce(Invoke(
          [&m_cb](Http::HeaderMap&, AsyncMatcher::Callbacks& callback) { m_cb = &callback; }));

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

// This test verifies AsyncMatcher::close() is called when Filter::onDestory() is called.
TEST_F(FilterTest, VerifyClose) {
  EXPECT_CALL(*raw_mock_matcher_, close()).Times(1);
  filter_->onDestroy();
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
