#include "source/extensions/filters/listener/http_inspector/http_inspector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {
namespace {

/**
 * These tests are written specifically for the NoOpParserCallbacks class to verify
 * that it's callback methods work as expected. This is necessary because the
 * HttpInspector filter only parses the request up to the first line break to determine the HTTP
 * version. As a result, not all callback methods are triggered in the HttpInspector tests.
 */
class NoOpParserCallbacksTest : public ::testing::Test {
protected:
  NoOpParserCallbacks callbacks_;
};

TEST_F(NoOpParserCallbacksTest, OnMessageBeginTest) {
  EXPECT_EQ(callbacks_.onMessageBegin(), Http::Http1::CallbackResult::Success);
}

TEST_F(NoOpParserCallbacksTest, OnUrlTest) {
  const absl::string_view url = "/index.html";
  EXPECT_EQ(callbacks_.onUrl(url.data(), url.size()), Http::Http1::CallbackResult::Success);
}

TEST_F(NoOpParserCallbacksTest, OnStatusTest) {
  const absl::string_view status = "200 OK";
  EXPECT_EQ(callbacks_.onStatus(status.data(), status.size()),
            Http::Http1::CallbackResult::Success);
}

TEST_F(NoOpParserCallbacksTest, OnHeaderFieldTest) {
  const absl::string_view header_field = "Content-Type";
  EXPECT_EQ(callbacks_.onHeaderField(header_field.data(), header_field.size()),
            Http::Http1::CallbackResult::Success);
}

TEST_F(NoOpParserCallbacksTest, OnHeaderValueTest) {
  const absl::string_view header_value = "text/html";
  EXPECT_EQ(callbacks_.onHeaderValue(header_value.data(), header_value.size()),
            Http::Http1::CallbackResult::Success);
}

TEST_F(NoOpParserCallbacksTest, OnHeadersCompleteTest) {
  EXPECT_EQ(callbacks_.onHeadersComplete(), Http::Http1::CallbackResult::Success);
}

TEST_F(NoOpParserCallbacksTest, BufferBodyTest) {
  const absl::string_view buffer_body = "buffer_body";
  callbacks_.bufferBody(buffer_body.data(), buffer_body.size());
}

TEST_F(NoOpParserCallbacksTest, OnMessageCompleteTest) {
  EXPECT_EQ(callbacks_.onMessageComplete(), Http::Http1::CallbackResult::Success);
}

TEST_F(NoOpParserCallbacksTest, OnChunkHeaderTest) {
  callbacks_.onChunkHeader(true);
  callbacks_.onChunkHeader(false);
}

} // namespace
} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
