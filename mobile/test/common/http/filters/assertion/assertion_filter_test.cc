#include "test/common/http/filters/assertion/config.h"
#include "test/common/http/filters/assertion/filter.h"
#include "test/common/http/filters/assertion/filter.pb.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ByMove;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Assertion {
namespace {

class AssertionFilterTest : public testing::Test {
public:
  void setUpFilter(std::string&& proto_str) {
    envoymobile::extensions::filters::http::assertion::Assertion config;
    Protobuf::TextFormat::ParseFromString(proto_str, &config);
    config_ = std::make_shared<AssertionFilterConfig>(config, context_);
    filter_ = std::make_unique<AssertionFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  AssertionFilterConfigSharedPtr config_{};
  std::unique_ptr<AssertionFilter> filter_{};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(AssertionFilterTest, RequestHeadersMatchWithEndStream) {
  setUpFilter(R"EOF(
match_config {
  http_request_headers_match {
    headers {
      name: ":authority"
      exact_match: "test.code"
    }
  }
}
)EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

TEST_F(AssertionFilterTest, RequestHeadersMatch) {
  setUpFilter(R"EOF(
  match_config {
  http_request_headers_match {
    headers {
      name: ":authority"
      exact_match: "test.code"
    }
  }
  }
)EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
}

TEST_F(AssertionFilterTest, RequestHeadersNoMatchWithEndStream) {
  setUpFilter(R"EOF(
  match_config {
  http_request_headers_match {
    headers {
      name: ":authority"
      exact_match: "test.code"
    }
  }
  }
)EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "no.match"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Headers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

TEST_F(AssertionFilterTest, RequestHeadersNoMatch) {
  setUpFilter(R"EOF(
  match_config {
  http_request_headers_match {
    headers {
      name: ":authority"
      exact_match: "test.code"
    }
  }
  }
)EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "no.match"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Headers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_F(AssertionFilterTest, RequestHeadersMatchWithEndstreamAndDataMissing) {
  setUpFilter(R"EOF(
match_config {
  and_match {
    rules {
      http_request_headers_match {
        headers {
          name: ":authority"
          exact_match: "test.code"
        }
      }
    }
    rules {
      http_request_generic_body_match {
        patterns {
          string_match: "match_me"
        }
      }
    }
  }
}
)EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Body does not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

TEST_F(AssertionFilterTest, RequestHeadersMatchWithEndstreamAndTrailersMissing) {
  setUpFilter(R"EOF(
match_config {
  and_match {
    rules {
      http_request_headers_match {
        headers {
          name: ":authority"
          exact_match: "test.code"
        }
      }
    }
    rules {
      http_request_trailers_match {
        headers {
          name: "test-trailer"
          exact_match: "test.code"
        }
      }
    }
  }
}
)EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Trailers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

TEST_F(AssertionFilterTest, RequestDataMatchWithEndStream) {
  setUpFilter(R"EOF(
match_config {
  http_request_generic_body_match {
    patterns {
      string_match: "match_me"
    }
  }
}
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("match_me")};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(*body, true));
}

TEST_F(AssertionFilterTest, RequestDataMatch) {
  setUpFilter(R"EOF(
match_config {
  http_request_generic_body_match {
    patterns {
      string_match: "match_me"
    }
  }
}
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("match_me")};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(*body, false));
}

TEST_F(AssertionFilterTest, RequestDataNoMatchFastPath) {
  setUpFilter(R"EOF(
match_config {
  http_request_generic_body_match {
    bytes_limit: 1
    patterns {
      string_match: "match_me"
    }
  }
}
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("garbage")};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Body does not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(*body, false));
}

TEST_F(AssertionFilterTest, RequestDataNoMatchWithEndStream) {
  setUpFilter(R"EOF(
match_config {
  http_request_generic_body_match {
    patterns {
      string_match: "match_me"
    }
  }
}
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("garbage")};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Body does not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(*body, true));
}

TEST_F(AssertionFilterTest, RequestDataMatchWithEndStreamAndTrailersMissing) {
  setUpFilter(R"EOF(
match_config {
  and_match {
    rules {
      http_request_generic_body_match {
        patterns {
          string_match: "match_me"
        }
      }
    }
    rules {
      http_request_trailers_match {
        headers {
          name: "test-trailer"
          exact_match: "test.code"
        }
      }
    }
  }
}
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("match_me")};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Trailers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(*body, true));
}

TEST_F(AssertionFilterTest, RequestDataNoMatchAfterTrailers) {
  setUpFilter(R"EOF(
match_config {
  and_match {
    rules {
      http_request_headers_match {
        headers {
          name: ":authority"
          exact_match: "test.code"
        }
      }
    }
    rules {
      http_request_generic_body_match {
        patterns {
          string_match: "match_me"
        }
      }
    }
  }
}
)EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};
  Buffer::InstancePtr body{new Buffer::OwnedImpl("garbage")};
  Http::TestRequestTrailerMapImpl request_trailers{{"test-trailer", "test.code"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Body does not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(*body, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));
}

TEST_F(AssertionFilterTest, RequestTrailersMatch) {
  setUpFilter(R"EOF(
  match_config {
  http_request_trailers_match {
    headers {
      name: "test-trailer"
      exact_match: "test.code"
    }
  }
}
)EOF");

  Http::TestRequestTrailerMapImpl request_trailers{{"test-trailer", "test.code"}};

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

TEST_F(AssertionFilterTest, RequestTrailersNoMatch) {
  setUpFilter(R"EOF(
match_config {
  http_request_trailers_match {
    headers {
      name: "test-trailer"
      exact_match: "test.code"
    }
  }
}
)EOF");

  Http::TestRequestTrailerMapImpl request_trailers{{"test-trailer", "no.match"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Trailers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));
}

TEST_F(AssertionFilterTest, ResponseHeadersMatchWithEndStream) {
  setUpFilter(R"EOF(
match_config {
  http_response_headers_match {
    headers {
      name: ":status"
      exact_match: "test.code"
    }
  }
}
)EOF");

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

TEST_F(AssertionFilterTest, ResponseHeadersMatch) {
  setUpFilter(R"EOF(
  match_config {
  http_response_headers_match {
    headers {
      name: ":status"
      exact_match: "test.code"
    }
  }
}
)EOF");

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
}

TEST_F(AssertionFilterTest, ResponseHeadersNoMatchWithEndStream) {
  setUpFilter(R"EOF(
match_config {
  http_response_headers_match {
    headers {
      name: ":status"
      exact_match: "test.code"
    }
  }
}
)EOF");

  Http::TestResponseHeaderMapImpl response_headers{{":status", "no.match"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "Response Headers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, true));
}

TEST_F(AssertionFilterTest, ResponseHeadersNoMatch) {
  setUpFilter(R"EOF(
  match_config {
  http_response_headers_match {
    headers {
      name: ":status"
      exact_match: "test.code"
    }
  }
}
)EOF");

  Http::TestResponseHeaderMapImpl response_headers{{":status", "no.match"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "Response Headers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
}

TEST_F(AssertionFilterTest, ResponseHeadersMatchWithEndstreamAndDataMissing) {
  setUpFilter(R"EOF(
match_config {
  and_match {
    rules {
      http_response_headers_match {
        headers {
          name: ":status"
          exact_match: "test.code"
        }
      }
    }
    rules {
      http_response_generic_body_match {
        patterns {
          string_match: "match_me"
        }
      }
    }
  }
}
)EOF");

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "Response Body does not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, true));
}

TEST_F(AssertionFilterTest, ResponseHeadersMatchWithEndstreamAndTrailersMissing) {
  setUpFilter(R"EOF(
match_config {
  and_match {
    rules {
      http_response_headers_match {
        headers {
          name: ":status"
          exact_match: "test.code"
        }
      }
    }
    rules {
      http_response_trailers_match {
        headers {
          name: "test-trailer"
          exact_match: "test.code"
        }
      }
    }
  }
}
)EOF");

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "Response Trailers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, true));
}

TEST_F(AssertionFilterTest, ResponseDataMatchWithEndStream) {
  setUpFilter(R"EOF(
match_config {
  http_response_generic_body_match {
    patterns {
      string_match: "match_me"
    }
  }
}
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("match_me")};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(*body, true));
}

TEST_F(AssertionFilterTest, ResponseDataMatch) {
  setUpFilter(R"EOF(
match_config {
  http_response_generic_body_match {
    patterns {
      string_match: "match_me"
    }
  }
}
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("match_me")};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(*body, false));
}

TEST_F(AssertionFilterTest, ResponseDataNoMatchFastPath) {
  setUpFilter(R"EOF(
match_config {
  http_response_generic_body_match {
    bytes_limit: 1
    patterns {
      string_match: "match_me"
    }
  }
}
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("garbage")};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "Response Body does not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(*body, false));
}

TEST_F(AssertionFilterTest, ResponseDataNoMatchWithEndStream) {
  setUpFilter(R"EOF(
match_config {
  http_response_generic_body_match {
    patterns {
      string_match: "match_me"
    }
  }
}
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("garbage")};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "Response Body does not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(*body, true));
}

TEST_F(AssertionFilterTest, ResponseDataMatchWithEndStreamAndTrailersMissing) {
  setUpFilter(R"EOF(
match_config {
  and_match {
    rules {
      http_response_generic_body_match {
        patterns {
          string_match: "match_me"
        }
      }
    }
    rules {
      http_response_trailers_match {
        headers {
          name: "test-trailer"
          exact_match: "test.code"
        }
      }
    }
  }
}
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("match_me")};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "Response Trailers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(*body, true));
}

TEST_F(AssertionFilterTest, ResponseDataNoMatchAfterTrailers) {
  setUpFilter(R"EOF(
match_config {
  and_match {
    rules {
      http_response_headers_match {
        headers {
          name: ":status"
          exact_match: "test.code"
        }
      }
    }
    rules {
      http_response_generic_body_match {
        patterns {
          string_match: "match_me"
        }
      }
    }
  }
}
)EOF");

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};
  Buffer::InstancePtr body{new Buffer::OwnedImpl("garbage")};
  Http::TestResponseTrailerMapImpl response_trailers{{"test-trailer", "test.code"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "Response Body does not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(*body, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers));
}

TEST_F(AssertionFilterTest, ResponseTrailersMatch) {
  setUpFilter(R"EOF(
match_config {
  http_response_trailers_match {
    headers {
      name: "test-trailer"
      exact_match: "test.code"
    }
  }
}
)EOF");

  Http::TestResponseTrailerMapImpl response_trailers{{"test-trailer", "test.code"}};

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
}

TEST_F(AssertionFilterTest, ResponseTrailersNoMatch) {
  setUpFilter(R"EOF(
match_config {
  http_response_trailers_match {
    headers {
      name: "test-trailer"
      exact_match: "test.code"
    }
  }
}
)EOF");

  Http::TestResponseTrailerMapImpl response_trailers{{"test-trailer", "no.match"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError,
                             "Response Trailers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers));
}

TEST(AssertionFilterFactoryTest, Config) {
#ifndef ENVOY_ENABLE_FULL_PROTOS
  // This proto is not defined in ../source/common/protobuf/create_reflectable_message.cc
  return;
#endif

  AssertionFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoymobile::extensions::filters::http::assertion::Assertion proto_config =
      *dynamic_cast<envoymobile::extensions::filters::http::assertion::Assertion*>(
          factory.createEmptyConfigProto().get());

  std::string config_str = R"EOF(
match_config {
  http_response_trailers_match {
    headers {
      name: "test-trailer"
      exact_match: "test.code"
    }
  }
}
)EOF";

  Protobuf::TextFormat::ParseFromString(config_str, &proto_config);

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "test", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

} // namespace
} // namespace Assertion
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
