#include "common/common/matchers.h"
#include "common/http/header_map_impl.h"

#include "extensions/filters/http/response_map/response_map_filter.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ResponseMapFilter {

const std::string& response_map_500_yaml = R"EOF(
response_map:
  mappers:
  - filter:
      status_code_filter:
        comparison:
          op: EQ
          value:
            default_value: 500
            runtime_key: _ignored
    body_format_override:
      text_format: 'response map: %RESPONSE_CODE%'
)EOF";

const std::string& response_map_500_html_yaml = R"EOF(
response_map:
  mappers:
  - filter:
      status_code_filter:
        comparison:
          op: EQ
          value:
            default_value: 500
            runtime_key: _ignored
    body_format_override:
      content_type: 'text/html'
      text_format: '<html>response map: %RESPONSE_CODE%</html>'
)EOF";

const std::string& response_map_500_empty_yaml = R"EOF(
response_map:
  mappers:
  - filter:
      status_code_filter:
        comparison:
          op: EQ
          value:
            default_value: 500
            runtime_key: _ignored
    body_format_override:
      text_format: ''
)EOF";

class ResponseMapFilterTest : public testing::Test {
public:
  ResponseMapFilterTest() {}

  void SetUpTest(const std::string& yaml) {
    envoy::extensions::filters::http::response_map::v3::ResponseMap proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config);

    config_ = std::make_shared<ResponseMapFilterConfig>(proto_config, "", factory_context_);
    filter_ = std::make_shared<ResponseMapFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);

    // Use a local encoding buffer, which will be modified by addEncodedData below to
    // preserve its side effects. This aligns with how the real encoder callbacks work
    // in practice, and this behavior is depended on by the response map filter's
    // content-type generation code.
    ON_CALL(encoder_callbacks_, encodingBuffer()).WillByDefault(Invoke([this] {
      return buffer_.get();
    }));
  }

  bool DoRewrite() { return filter_->do_rewrite_; }

  bool Disabled() { return filter_->disabled_; }

protected:
  void TestNoMatch(const std::string& yaml, const std::string& original_body) {
    SetUpTest(yaml);

    // Decode headers with end_stream=true. We always continue iteration during decoding.
    Http::TestRequestHeaderMapImpl request_headers{{":method", "get"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

    // We should not modify the encoding buffer nor add data to it, because the
    // filter is passing through.
    EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_)).Times(0);
    EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _)).Times(0);

    // Encode response headers with a status that does not match the mapper.
    // Expect to continue iteration, because the filter is passing through.
    // Only end stream if the original response body is empty.
    Buffer::OwnedImpl response_data{original_body};
    Http::TestResponseHeaderMapImpl response_headers{{":status", "501"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue,
              filter_->encodeHeaders(response_headers, !original_body.empty()));

    // Encode the original response body if it isn't empty.
    if (!original_body.empty()) {
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, true));
    }

    // The filter was not disabled by any per-route config, and it should not have done
    // any rewrite. Therefore there should be nothing in buffer_, and there should be
    // no content-type nor content-length headers.
    EXPECT_EQ(false, Disabled());
    EXPECT_EQ(false, DoRewrite());
    EXPECT_EQ(nullptr, buffer_.get());
    EXPECT_EQ("", std::string(response_headers.getContentTypeValue()));
    EXPECT_EQ("", std::string(response_headers.getContentLengthValue()));
  }

  void TestMatch(const std::string& yaml, const std::string& original_body,
                 const std::string& expected_body) {
    TestMatchContentType(yaml, original_body, expected_body, "", "text/plain");
  }

  void TestMatchContentType(const std::string& yaml, const std::string& original_body,
                            const std::string& expected_body,
                            const std::string& original_content_type,
                            const std::string& expected_content_type) {
    SetUpTest(yaml);

    // Decode headers with end_stream=true. We always continue iteration during decoding.
    Http::TestRequestHeaderMapImpl request_headers{{":method", "get"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

    if (original_body.empty()) {
      // We add encoded data (not modify it) because there is no original response body upstream.
      EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_)).Times(0);
      EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
          .WillOnce(Invoke([this, expected_body](Buffer::Instance& data, bool) {
            // Expect that the encoded data is correct, and save it to our local buffer.
            EXPECT_EQ(expected_body, data.toString());
            buffer_ = std::move(Buffer::InstancePtr{new Buffer::OwnedImpl(data)});
          }));
    } else {
      // We modify encoded data (not "add" it) because there is an original response body upstream.
      EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
          .WillOnce(
              Invoke([this, expected_body](std::function<void(::Envoy::Buffer::Instance&)> fn) {
                // Let `fn` modify the response body buffer_.
                // Expect that the result is the rewritten response body.
                fn(*buffer_);
                EXPECT_EQ(expected_body, buffer_->toString());
              }));
      EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _)).Times(0);
    }

    // Encode response headers with a status that matches the mapper.
    Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
    if (!original_content_type.empty()) {
      response_headers.addCopy("content-type", original_content_type);
    }

    if (original_body.empty()) {
      // Expect to continue iteration, because we only stop iteration when
      // end_stream=false to replace a response body.
      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                filter_->encodeHeaders(response_headers, true));
    } else {
      // Expect to stop iteration, because end_stream=false and we are replacing
      // the response body.
      EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
                filter_->encodeHeaders(response_headers, false));

      // Encode the original body.
      // We need to pass an OwnedImpl of the original encoded data, because encodeData
      // will drain it before populating buffer_ with the new response body. In practice,
      // the encoding buffer (buffer_) needs to be empty if we are draining all of the
      // encoded data we see in the filter, so set buffer_ to empty here.
      Buffer::OwnedImpl response_data{original_body};
      buffer_ = std::move(Buffer::InstancePtr(new Buffer::OwnedImpl()));
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, true));

      // The response_data passed to encodeData should have been drained.
      EXPECT_EQ(0, response_data.length());
    }

    // The filter was not disabled by any per-route config, and it should have done
    // a rewrite using the default content-type of "text/plain" with a content-length
    // equal to the length of the new body. The buffer_ member should have been modified
    // to the new response body.
    EXPECT_EQ(false, Disabled());
    EXPECT_EQ(true, DoRewrite());
    EXPECT_EQ(expected_body, buffer_->toString());
    EXPECT_EQ(expected_content_type, std::string(response_headers.getContentTypeValue()));
    EXPECT_EQ(std::to_string(buffer_->length()),
              std::string(response_headers.getContentLengthValue()));
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::shared_ptr<ResponseMapFilterConfig> config_;
  std::shared_ptr<ResponseMapFilter> filter_;
  Buffer::InstancePtr buffer_;
};

// We should do nothing when the response does not match the mapper
// and there is no original body upstream.
TEST_F(ResponseMapFilterTest, NoMatchNoOriginalBody) { TestNoMatch(response_map_500_yaml, ""); }

// We should do nothing when the response does not match the mapper
// and there is no original body upstream.
TEST_F(ResponseMapFilterTest, NoMatchWithOriginalBody) {
  TestNoMatch(response_map_500_yaml, "500 response generated upstream");
}

// We should add a new body if the matcher matches and there is no
// original body upstream.
TEST_F(ResponseMapFilterTest, MatchNoOriginalBody) {
  TestMatch(response_map_500_yaml, "", "response map: 500");
}

// We should replace the original body if the mapper matches and
// there is an original body upstream.
TEST_F(ResponseMapFilterTest, MatchWithOriginalBody) {
  TestMatch(response_map_500_yaml, "500 response generated upstream", "response map: 500");
}

// We should replace the original body with the empty body if the mapper
// matches and there is an original body upstream.
TEST_F(ResponseMapFilterTest, MatchWithOriginalBodyReplaceEmpty) {
  TestMatch(response_map_500_empty_yaml, "500 response generated upstream", "");
}

// We should add a new body and content-type if the matcher matches and there is
// no original body upstream nor original content type.
TEST_F(ResponseMapFilterTest, MatchHtmlNoOriginalBodyNoContentType) {
  TestMatchContentType(response_map_500_html_yaml, "", "<html>response map: 500</html>", "",
                       "text/html");
}

// We should replace the original body and add a content-type if the matcher
// matches and there is an original body upstream but no original content type.
TEST_F(ResponseMapFilterTest, MatchHtmlWithOriginalBodyNoContentType) {
  TestMatchContentType(response_map_500_html_yaml, "non-html 500 response generated upstream",
                       "<html>response map: 500</html>", "", "text/html");
}

// We should replace the original body and the original content type if the
// matcher matches and there is no original body but there is an original
// content type.
TEST_F(ResponseMapFilterTest, MatchHtmlNoOriginalBodyAndContentType) {
  TestMatchContentType(response_map_500_html_yaml, "", "<html>response map: 500</html>",
                       "text/plain", "text/html");
}

// We should replace the original body and the original content type if the
// matcher matches and there is an original body and an original content type.
TEST_F(ResponseMapFilterTest, MatchHtmlWithOriginalBodyAndContentType) {
  TestMatchContentType(response_map_500_html_yaml, "non-html 500 response generated upstream",
                       "<html>response map: 500</html>", "text/plain", "text/html");
}

} // namespace ResponseMapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
