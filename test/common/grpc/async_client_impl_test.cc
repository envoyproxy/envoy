#include "common/common/enum_to_int.h"
#include "common/grpc/async_client_impl.h"
#include "common/grpc/common.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Grpc {
namespace {

const std::string HELLO_REQUEST = "ABC";
// We expect the 5 byte header to only have a length of 5 indicating the size of the protobuf. The
// protobuf begins with 0x0a, indicating this is the first field of type string. This is followed
// by 0x03 for the number of characters and the name ABC set above.
const char HELLO_REQUEST_DATA[] = "\x00\x00\x00\x00\x05\x0a\x03\x41\x42\x43";
const size_t HELLO_REQUEST_SIZE = sizeof(HELLO_REQUEST_DATA) - 1;

const std::string HELLO_REPLY = "DEFG";
const char HELLO_REPLY_DATA[] = "\x00\x00\x00\x00\x06\x0a\x04\x44\x45\x46\x47";
const size_t HELLO_REPLY_SIZE = sizeof(HELLO_REPLY_DATA) - 1;

MATCHER_P(HelloworldReplyEq, rhs, "") { return arg.message() == rhs; }

typedef std::vector<std::pair<Http::LowerCaseString, std::string>> TestMetadata;

class HelloworldStream : public MockAsyncStreamCallbacks<helloworld::HelloReply> {
public:
  HelloworldStream(Http::MockAsyncClientStream* http_stream) : http_stream_(http_stream) {
    ON_CALL(*http_stream_, reset()).WillByDefault(Invoke([this]() { http_callbacks_->onReset(); }));
  }

  ~HelloworldStream() { Mock::VerifyAndClear(http_stream_); }

  void sendRequest(bool end_stream = false) {
    helloworld::HelloRequest request;
    request.set_name(HELLO_REQUEST);

    EXPECT_CALL(*http_stream_,
                sendData(BufferStringEqual(std::string(HELLO_REQUEST_DATA, HELLO_REQUEST_SIZE)),
                         end_stream));
    grpc_stream_->sendMessage(request, end_stream);
    Mock::VerifyAndClearExpectations(http_stream_);
  }

  void sendServerInitialMetadata(TestMetadata& metadata) {
    Http::HeaderMapPtr reply_headers{new Http::TestHeaderMapImpl{{":status", "200"}}};
    for (auto& value : metadata) {
      reply_headers->addReference(value.first, value.second);
    }
    EXPECT_CALL(*this, onReceiveInitialMetadata_(HeaderMapEqualRef(reply_headers.get())));
    http_callbacks_->onHeaders(std::move(reply_headers), false);
  }

  void sendReply() {
    Buffer::OwnedImpl reply_buffer(HELLO_REPLY_DATA, HELLO_REPLY_SIZE);

    helloworld::HelloReply reply;
    reply.set_message(HELLO_REPLY);
    EXPECT_CALL(*this, onReceiveMessage_(HelloworldReplyEq(HELLO_REPLY)));
    http_callbacks_->onData(reply_buffer, false);
  }

  void expectGrpcStatus(Status::GrpcStatus grpc_status,
                        const std::string& grpc_message = std::string()) {
    if (grpc_status != Status::GrpcStatus::Ok) {
      EXPECT_CALL(*http_stream_, reset());
    }
    EXPECT_CALL(*this, onRemoteClose(grpc_status, grpc_message))
        .WillOnce(Invoke([this](Status::GrpcStatus grpc_status, const std::string&) {
          if (grpc_status != Status::GrpcStatus::Ok) {
            clearStream();
          }
        }));
  }

  void sendServerTrailers(Status::GrpcStatus grpc_status, const std::string& grpc_message,
                          TestMetadata metadata, bool trailers_only = false) {
    auto* reply_trailers =
        new Http::TestHeaderMapImpl{{"grpc-status", std::to_string(enumToInt(grpc_status))}};
    if (!grpc_message.empty()) {
      reply_trailers->addCopy("grpc-message", grpc_message);
    }
    if (trailers_only) {
      reply_trailers->addCopy(":status", "200");
    }
    for (const auto& value : metadata) {
      reply_trailers->addCopy(value.first, value.second);
    }
    Http::HeaderMapPtr reply_trailers_ptr{reply_trailers};
    if (grpc_status == Status::GrpcStatus::Ok) {
      EXPECT_CALL(*this, onReceiveTrailingMetadata_(HeaderMapEqualRef(reply_trailers)));
    }
    expectGrpcStatus(grpc_status, grpc_message);
    if (trailers_only) {
      http_callbacks_->onHeaders(std::move(reply_trailers_ptr), true);
    } else {
      http_callbacks_->onTrailers(std::move(reply_trailers_ptr));
    }
  }

  void closeStream() {
    EXPECT_CALL(*http_stream_, sendData(BufferStringEqual(""), true));
    EXPECT_CALL(*http_stream_, reset());
    grpc_stream_->closeStream();
    clearStream();
  }

  void clearStream() { grpc_stream_ = nullptr; }

  Http::AsyncClient::StreamCallbacks* http_callbacks_{};
  Http::MockAsyncClientStream* http_stream_;
  AsyncStream* grpc_stream_{};
};

class HelloworldRequest : public MockAsyncRequestCallbacks<helloworld::HelloReply> {
public:
  HelloworldRequest(Http::MockAsyncClientStream* http_stream) : http_stream_(http_stream) {}

  ~HelloworldRequest() { Mock::VerifyAndClear(http_stream_); }

  void sendReply() {
    Http::HeaderMapPtr reply_headers{new Http::TestHeaderMapImpl{{":status", "200"}}};
    http_callbacks_->onHeaders(std::move(reply_headers), false);

    Buffer::OwnedImpl reply_buffer(HELLO_REPLY_DATA, HELLO_REPLY_SIZE);
    helloworld::HelloReply reply;
    reply.set_message(HELLO_REPLY);
    http_callbacks_->onData(reply_buffer, false);

    Http::HeaderMapPtr reply_trailers{new Http::TestHeaderMapImpl{{"grpc-status", "0"}}};
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().GRPC_STATUS_CODE, "0"));
    EXPECT_CALL(*this, onSuccess_(HelloworldReplyEq(HELLO_REPLY), _));
    EXPECT_CALL(*child_span_, finishSpan());
    EXPECT_CALL(*http_stream_, reset());
    http_callbacks_->onTrailers(std::move(reply_trailers));
  }

  Http::AsyncClient::StreamCallbacks* http_callbacks_{};
  Http::MockAsyncClientStream* http_stream_;
  AsyncRequest* grpc_request_{};
  Tracing::MockSpan* child_span_{new Tracing::MockSpan()};
};

class GrpcAsyncClientImplTest : public testing::Test {
public:
  GrpcAsyncClientImplTest()
      : method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")),
        grpc_client_(new AsyncClientImpl(cm_, "test_cluster")) {
    ON_CALL(cm_, httpAsyncClientForCluster("test_cluster")).WillByDefault(ReturnRef(http_client_));
  }

  ~GrpcAsyncClientImplTest() {
    for (auto* stream : dangling_streams_) {
      EXPECT_CALL(*stream, reset());
    }
  }

  Http::TestHeaderMapImpl expectedHeaders(const TestMetadata& initial_metadata) const {
    Http::TestHeaderMapImpl headers{{":method", "POST"},
                                    {":path", "/helloworld.Greeter/SayHello"},
                                    {":authority", "test_cluster"},
                                    {"content-type", "application/grpc"},
                                    {"te", "trailers"}};
    for (auto& value : initial_metadata) {
      headers.addReference(value.first, value.second);
    }

    return headers;
  }

  std::unique_ptr<HelloworldRequest> createRequest(const TestMetadata& initial_metadata) {
    http_streams_.emplace_back(new Http::MockAsyncClientStream());
    std::unique_ptr<HelloworldRequest> request(new HelloworldRequest(http_streams_.back().get()));
    EXPECT_CALL(*request, onCreateInitialMetadata(_))
        .WillOnce(Invoke([&initial_metadata](Http::HeaderMap& headers) {
          for (auto& value : initial_metadata) {
            headers.addReference(value.first, value.second);
          }
        }));
    const auto headers = expectedHeaders(initial_metadata);
    EXPECT_CALL(http_client_, start(_, _, true))
        .WillOnce(Invoke([&request](Http::AsyncClient::StreamCallbacks& callbacks,
                                    const Optional<std::chrono::milliseconds>&, bool) {
          request->http_callbacks_ = &callbacks;
          return request->http_stream_;
        }));

    const Http::HeaderMapImpl* active_header_map = nullptr;
    EXPECT_CALL(*(request->http_stream_), sendHeaders(HeaderMapEqualRef(&headers), _))
        .WillOnce(Invoke([&active_header_map](Http::HeaderMap& headers, bool) {
          active_header_map = dynamic_cast<const Http::HeaderMapImpl*>(&headers);
        }));
    helloworld::HelloRequest request_msg;
    request_msg.set_name(HELLO_REQUEST);
    EXPECT_CALL(
        *(request->http_stream_),
        sendData(BufferStringEqual(std::string(HELLO_REQUEST_DATA, HELLO_REQUEST_SIZE)), true));

    Tracing::MockSpan active_span;

    EXPECT_CALL(active_span, spawnChild_(_, "async test_cluster egress", _))
        .WillOnce(Return(request->child_span_));
    EXPECT_CALL(*request->child_span_,
                setTag(Tracing::Tags::get().UPSTREAM_CLUSTER, "test_cluster"));
    EXPECT_CALL(*request->child_span_,
                setTag(Tracing::Tags::get().COMPONENT, Tracing::Tags::get().PROXY));
    EXPECT_CALL(*request->child_span_, injectContext(_));

    request->grpc_request_ = grpc_client_->send(*method_descriptor_, request_msg, *request,
                                                active_span, Optional<std::chrono::milliseconds>());
    EXPECT_NE(request->grpc_request_, nullptr);
    // The header map should still be valid after grpc_client_->start() returns, since it is
    // retained by the HTTP async client for the deferred send.
    EXPECT_EQ(*active_header_map, headers);
    return request;
  }

  std::unique_ptr<HelloworldStream> createStream(TestMetadata& initial_metadata) {
    http_streams_.emplace_back(new Http::MockAsyncClientStream());
    std::unique_ptr<HelloworldStream> stream(new HelloworldStream(http_streams_.back().get()));
    EXPECT_CALL(*stream, onCreateInitialMetadata(_))
        .WillOnce(Invoke([&initial_metadata](Http::HeaderMap& headers) {
          for (auto& value : initial_metadata) {
            headers.addReference(value.first, value.second);
          }
        }));
    const auto headers = expectedHeaders(initial_metadata);
    EXPECT_CALL(http_client_, start(_, _, false))
        .WillOnce(Invoke([&stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                   const Optional<std::chrono::milliseconds>&, bool) {
          stream->http_callbacks_ = &callbacks;
          return stream->http_stream_;
        }));
    const Http::HeaderMapImpl* active_header_map = nullptr;
    EXPECT_CALL(*(stream->http_stream_), sendHeaders(HeaderMapEqualRef(&headers), _))
        .WillOnce(Invoke([&active_header_map](Http::HeaderMap& headers, bool) {
          active_header_map = dynamic_cast<const Http::HeaderMapImpl*>(&headers);
        }));
    stream->grpc_stream_ = grpc_client_->start(*method_descriptor_, *stream);
    EXPECT_NE(stream->grpc_stream_, nullptr);
    // The header map should still be valid after grpc_client_->start() returns, since it is
    // retained by the HTTP async client for the deferred send.
    EXPECT_EQ(*active_header_map, headers);
    return stream;
  }

  void expectResetOn(HelloworldStream* stream) {
    dangling_streams_.push_back(stream->http_stream_);
  }

  std::vector<Http::MockAsyncClientStream*> dangling_streams_;
  std::vector<std::unique_ptr<Http::MockAsyncClientStream>> http_streams_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  NiceMock<Http::MockAsyncClient> http_client_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::unique_ptr<AsyncClientImpl> grpc_client_;
};

// Validate that a simple request-reply stream works.
TEST_F(GrpcAsyncClientImplTest, BasicStream) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  stream->sendServerTrailers(Status::GrpcStatus::Ok, "", empty_metadata);
  stream->closeStream();
}

// Validate that a simple request-reply unary RPC works.
TEST_F(GrpcAsyncClientImplTest, BasicRequest) {
  TestMetadata empty_metadata;
  auto request = createRequest(empty_metadata);
  request->sendReply();
}

// Validate that multiple streams work.
TEST_F(GrpcAsyncClientImplTest, MultiStream) {
  TestMetadata empty_metadata;
  auto stream_0 = createStream(empty_metadata);
  auto stream_1 = createStream(empty_metadata);
  stream_0->sendRequest();
  stream_1->sendRequest();
  stream_0->sendServerInitialMetadata(empty_metadata);
  stream_0->sendReply();
  stream_1->sendServerTrailers(Status::GrpcStatus::Unavailable, "", empty_metadata);
  stream_0->sendServerTrailers(Status::GrpcStatus::Ok, "", empty_metadata);
  stream_0->closeStream();
}

// Validate that multiple request-reply unary RPCs works.
TEST_F(GrpcAsyncClientImplTest, MultiRequest) {
  TestMetadata empty_metadata;
  auto request_0 = createRequest(empty_metadata);
  auto request_1 = createRequest(empty_metadata);
  request_1->sendReply();
  request_0->sendReply();
}

// Validate that a failure in the HTTP client returns immediately with status
// UNAVAILABLE.
TEST_F(GrpcAsyncClientImplTest, StreamHttpStartFail) {
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(http_client_, start(_, _, false)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks, onRemoteClose(Status::GrpcStatus::Unavailable, ""));
  auto* grpc_stream = grpc_client_->start(*method_descriptor_, grpc_callbacks);
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that a failure in the HTTP client returns immediately with status
// UNAVAILABLE.
TEST_F(GrpcAsyncClientImplTest, RequestHttpStartFail) {
  MockAsyncRequestCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(http_client_, start(_, _, true)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks, onFailure(Status::GrpcStatus::Unavailable, "", _));
  helloworld::HelloRequest request_msg;

  Tracing::MockSpan active_span;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(active_span, spawnChild_(_, "async test_cluster egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().COMPONENT, Tracing::Tags::get().PROXY));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().UPSTREAM_CLUSTER, "test_cluster"));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().GRPC_STATUS_CODE, "14"));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE));
  EXPECT_CALL(*child_span, finishSpan());
  EXPECT_CALL(*child_span, injectContext(_)).Times(0);

  auto* grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                          active_span, Optional<std::chrono::milliseconds>());
  EXPECT_EQ(grpc_request, nullptr);
}

// Validate that a failure to sendHeaders() in the HTTP client returns
// immediately with status INTERNAL.
TEST_F(GrpcAsyncClientImplTest, StreamHttpSendHeadersFail) {
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;
  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _, false))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Optional<std::chrono::milliseconds>&, bool) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));
  EXPECT_CALL(grpc_callbacks, onCreateInitialMetadata(_));
  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap& headers, bool end_stream) {
        UNREFERENCED_PARAMETER(headers);
        UNREFERENCED_PARAMETER(end_stream);
        http_callbacks->onReset();
      }));
  EXPECT_CALL(grpc_callbacks, onRemoteClose(Status::GrpcStatus::Internal, ""));
  auto* grpc_stream = grpc_client_->start(*method_descriptor_, grpc_callbacks);
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that a failure to sendHeaders() in the HTTP client returns
// immediately with status INTERNAL.
TEST_F(GrpcAsyncClientImplTest, RequestHttpSendHeadersFail) {
  MockAsyncRequestCallbacks<helloworld::HelloReply> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;
  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _, true))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Optional<std::chrono::milliseconds>&, bool) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));
  EXPECT_CALL(grpc_callbacks, onCreateInitialMetadata(_));
  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap& headers, bool end_stream) {
        UNREFERENCED_PARAMETER(headers);
        UNREFERENCED_PARAMETER(end_stream);
        http_callbacks->onReset();
      }));
  EXPECT_CALL(grpc_callbacks, onFailure(Status::GrpcStatus::Internal, "", _));
  helloworld::HelloRequest request_msg;

  Tracing::MockSpan active_span;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(active_span, spawnChild_(_, "async test_cluster egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().COMPONENT, Tracing::Tags::get().PROXY));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().UPSTREAM_CLUSTER, "test_cluster"));
  EXPECT_CALL(*child_span, injectContext(_));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().GRPC_STATUS_CODE, "13"));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE));
  EXPECT_CALL(*child_span, finishSpan());

  auto* grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                          active_span, Optional<std::chrono::milliseconds>());
  EXPECT_EQ(grpc_request, nullptr);
}

// Validate that a non-200 HTTP status results in the gRPC error as per
// https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
TEST_F(GrpcAsyncClientImplTest, HttpNon200Status) {
  for (const auto http_response_status : {400, 401, 403, 404, 429, 431}) {
    TestMetadata empty_metadata;
    auto stream = createStream(empty_metadata);
    Http::HeaderMapPtr reply_headers{
        new Http::TestHeaderMapImpl{{":status", std::to_string(http_response_status)}}};
    stream->expectGrpcStatus(Common::httpToGrpcStatus(http_response_status));
    stream->http_callbacks_->onHeaders(std::move(reply_headers), false);
  }
}

// Validate that a non-200 HTTP status results in fallback to grpc-status.
TEST_F(GrpcAsyncClientImplTest, GrpcStatusFallback) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  Http::HeaderMapPtr reply_headers{new Http::TestHeaderMapImpl{
      {":status", "404"},
      {"grpc-status", std::to_string(enumToInt(Status::GrpcStatus::PermissionDenied))},
      {"grpc-message", "error message"}}};
  stream->expectGrpcStatus(Status::GrpcStatus::PermissionDenied, "error message");
  stream->http_callbacks_->onHeaders(std::move(reply_headers), true);
}

// Validate that a HTTP-level reset is handled as an INTERNAL gRPC error.
TEST_F(GrpcAsyncClientImplTest, HttpReset) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  EXPECT_CALL(*stream, onRemoteClose(Status::GrpcStatus::Internal, ""));
  stream->http_callbacks_->onReset();
  stream->clearStream();
}

// Validate that a reply with bad gRPC framing is handled as an INTERNAL gRPC
// error.
TEST_F(GrpcAsyncClientImplTest, BadReplyGrpcFraming) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata);
  stream->expectGrpcStatus(Status::GrpcStatus::Internal);
  Buffer::OwnedImpl reply_buffer("\xde\xad\xbe\xef\x00", 5);
  stream->http_callbacks_->onData(reply_buffer, false);
}

// Validate that a reply with bad protobuf is handled as an INTERNAL gRPC error.
TEST_F(GrpcAsyncClientImplTest, BadReplyProtobuf) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata);
  stream->expectGrpcStatus(Status::GrpcStatus::Internal);
  Buffer::OwnedImpl reply_buffer("\x00\x00\x00\x00\x02\xff\xff", 7);
  stream->http_callbacks_->onData(reply_buffer, false);
}

// Validate that an out-of-range gRPC status is handled as an INVALID_CODE gRPC
// error.
TEST_F(GrpcAsyncClientImplTest, OutOfRangeGrpcStatus) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  stream->expectGrpcStatus(Status::GrpcStatus::InvalidCode);
  Http::HeaderMapPtr reply_trailers{
      new Http::TestHeaderMapImpl{{"grpc-status", std::to_string(0x1337)}}};
  stream->http_callbacks_->onTrailers(std::move(reply_trailers));
}

// Validate that a missing gRPC status is handled as an INTERNAL gRPC error.
TEST_F(GrpcAsyncClientImplTest, MissingGrpcStatus) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  stream->expectGrpcStatus(Status::GrpcStatus::Internal);
  Http::HeaderMapPtr reply_trailers{new Http::TestHeaderMapImpl{}};
  stream->http_callbacks_->onTrailers(std::move(reply_trailers));
}

// Validate that a reply terminated without trailers is handled as an INTERNAL
// gRPC error.
TEST_F(GrpcAsyncClientImplTest, ReplyNoTrailers) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata);
  stream->expectGrpcStatus(Status::GrpcStatus::Internal);
  Buffer::OwnedImpl reply_buffer(HELLO_REPLY_DATA, HELLO_REPLY_SIZE);
  helloworld::HelloReply reply;
  reply.set_message(HELLO_REPLY);
  stream->http_callbacks_->onData(reply_buffer, true);
}

// Validate that send client initial metadata works.
TEST_F(GrpcAsyncClientImplTest, StreamClientInitialMetadata) {
  TestMetadata initial_metadata = {
      {Http::LowerCaseString("foo"), "bar"},
      {Http::LowerCaseString("baz"), "blah"},
  };
  auto stream = createStream(initial_metadata);
  expectResetOn(stream.get());
}

// Validate that send client initial metadata works.
TEST_F(GrpcAsyncClientImplTest, RequestClientInitialMetadata) {
  TestMetadata initial_metadata = {
      {Http::LowerCaseString("foo"), "bar"},
      {Http::LowerCaseString("baz"), "blah"},
  };
  auto request = createRequest(initial_metadata);
  dangling_streams_.push_back(request->http_stream_);
}

// Validate that receiving server initial metadata works.
TEST_F(GrpcAsyncClientImplTest, ServerInitialMetadata) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest();
  TestMetadata initial_metadata = {
      {Http::LowerCaseString("foo"), "bar"},
      {Http::LowerCaseString("baz"), "blah"},
  };
  stream->sendServerInitialMetadata(initial_metadata);
  expectResetOn(stream.get());
}

// Validate that receiving server trailing metadata works.
TEST_F(GrpcAsyncClientImplTest, ServerTrailingMetadata) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  TestMetadata trailing_metadata = {
      {Http::LowerCaseString("foo"), "bar"},
      {Http::LowerCaseString("baz"), "blah"},
  };
  stream->sendServerTrailers(Status::GrpcStatus::Ok, "", trailing_metadata);
  expectResetOn(stream.get());
}

// Validate that a trailers-only response is handled for streams.
TEST_F(GrpcAsyncClientImplTest, StreamTrailersOnly) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendServerTrailers(Status::GrpcStatus::Ok, "", empty_metadata, true);
  stream->closeStream();
}

// Validate that a trailers-only response is handled for requests, where it is
// an error.
TEST_F(GrpcAsyncClientImplTest, RequestTrailersOnly) {
  TestMetadata empty_metadata;
  auto request = createRequest(empty_metadata);
  Http::HeaderMapPtr reply_headers{
      new Http::TestHeaderMapImpl{{":status", "200"}, {"grpc-status", "0"}}};
  EXPECT_CALL(*request->child_span_, setTag(Tracing::Tags::get().GRPC_STATUS_CODE, "0"));
  EXPECT_CALL(*request->child_span_, setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE));
  EXPECT_CALL(*request, onFailure(Status::Internal, "", _));
  EXPECT_CALL(*request->child_span_, finishSpan());
  EXPECT_CALL(*request->http_stream_, reset());
  request->http_callbacks_->onTrailers(std::move(reply_headers));
}

// Validate that a trailers RESOURCE_EXHAUSTED reply is handled.
TEST_F(GrpcAsyncClientImplTest, ResourceExhaustedError) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  stream->sendServerTrailers(Status::GrpcStatus::ResourceExhausted, "error message",
                             empty_metadata);
}

// Validate that we can continue to receive after a local close.
TEST_F(GrpcAsyncClientImplTest, ReceiveAfterLocalClose) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest(true);
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  EXPECT_CALL(*stream->http_stream_, reset());
  stream->sendServerTrailers(Status::GrpcStatus::Ok, "", empty_metadata);
}

// Validate that we can continue to send after a remote close.
TEST_F(GrpcAsyncClientImplTest, SendAfterRemoteClose) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  stream->sendServerTrailers(Status::GrpcStatus::Ok, "", empty_metadata);
  stream->sendRequest();
  stream->closeStream();
}

// Validate that reset() doesn't explode on a half-closed stream (local).
TEST_F(GrpcAsyncClientImplTest, ResetAfterCloseLocal) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  EXPECT_CALL(*stream->http_stream_, sendData(BufferStringEqual(""), true));
  stream->grpc_stream_->closeStream();
  EXPECT_CALL(*stream->http_stream_, reset());
  stream->grpc_stream_->resetStream();
  stream->clearStream();
}

// Validate that reset() doesn't explode on a half-closed stream (remote).
TEST_F(GrpcAsyncClientImplTest, ResetAfterCloseRemote) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendServerTrailers(Status::GrpcStatus::Ok, "", empty_metadata, true);
  EXPECT_CALL(*stream->http_stream_, reset());
  stream->grpc_stream_->resetStream();
  stream->clearStream();
}

// Validate that request cancel() works.
TEST_F(GrpcAsyncClientImplTest, CancelRequest) {
  TestMetadata empty_metadata;
  auto request = createRequest(empty_metadata);
  EXPECT_CALL(*request->child_span_,
              setTag(Tracing::Tags::get().STATUS, Tracing::Tags::get().CANCELED));
  EXPECT_CALL(*request->child_span_, finishSpan());
  EXPECT_CALL(*request->http_stream_, reset());
  request->grpc_request_->cancel();
}

} // namespace
} // namespace Grpc
} // namespace Envoy
