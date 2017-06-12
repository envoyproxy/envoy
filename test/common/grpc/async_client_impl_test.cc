#include "common/grpc/async_client_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::_;
using testing::Invoke;
using testing::Eq;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::Mock;

namespace Grpc {

template class AsyncClientImpl<helloworld::HelloRequest, helloworld::HelloReply>;
template class AsyncClientStreamImpl<helloworld::HelloRequest, helloworld::HelloReply>;

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

class HelloworldStream : public MockAsyncClientCallbacks<helloworld::HelloReply> {
public:
  HelloworldStream() {
    ON_CALL(http_stream_, reset()).WillByDefault(Invoke([this]() { http_callbacks_->onReset(); }));
  }

  ~HelloworldStream() {
    if (grpc_stream_ != nullptr) {
      EXPECT_CALL(http_stream_, reset());
      grpc_stream_->reset();
    }
  }

  void sendRequest() {
    helloworld::HelloRequest request;
    request.set_name(HELLO_REQUEST);

    EXPECT_CALL(
        http_stream_,
        sendData(BufferStringEqual(std::string(HELLO_REQUEST_DATA, HELLO_REQUEST_SIZE)), false));
    grpc_stream_->sendMessage(request);
    Mock::VerifyAndClearExpectations(&http_stream_);
  }

  void sendServerInitialMetadata(TestMetadata& metadata) {
    Http::HeaderMapPtr reply_headers{new Http::TestHeaderMapImpl{{":status", "200"}}};
    for (auto& value : metadata) {
      reply_headers->addStatic(value.first, value.second);
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

  void expectGrpcStatus(Status::GrpcStatus grpc_status) {
    if (grpc_status != Status::GrpcStatus::Ok) {
      EXPECT_CALL(http_stream_, reset());
    }
    EXPECT_CALL(*this, onRemoteClose(grpc_status))
        .WillOnce(Invoke([this](Status::GrpcStatus grpc_status) {
          if (grpc_status != Status::GrpcStatus::Ok) {
            clearStream();
          }
        }));
  }

  void sendServerTrailers(Status::GrpcStatus grpc_status, TestMetadata metadata,
                          bool trailers_only = false) {
    Http::HeaderMapPtr reply_trailers{
        new Http::TestHeaderMapImpl{{"grpc-status", std::to_string(enumToInt(grpc_status))}}};
    if (trailers_only) {
      reply_trailers->addStatic(Http::LowerCaseString(":status"), "200");
    }
    for (auto& value : metadata) {
      reply_trailers->addStatic(value.first, value.second);
    }
    EXPECT_CALL(*this, onReceiveTrailingMetadata_(HeaderMapEqualRef(reply_trailers.get())));
    expectGrpcStatus(grpc_status);
    if (trailers_only) {
      http_callbacks_->onHeaders(std::move(reply_trailers), true);
    } else {
      http_callbacks_->onTrailers(std::move(reply_trailers));
    }
  }

  void closeStream() {
    EXPECT_CALL(http_stream_, reset());
    grpc_stream_->close();
    clearStream();
  }

  void clearStream() { grpc_stream_ = nullptr; }

  Http::AsyncClient::StreamCallbacks* http_callbacks_{};
  Http::MockAsyncClientStream http_stream_;
  AsyncClientStream<helloworld::HelloRequest>* grpc_stream_{};
};

class GrpcAsyncClientImplTest : public testing::Test {
public:
  GrpcAsyncClientImplTest()
      : method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")),
        grpc_client_(new AsyncClientImpl<helloworld::HelloRequest, helloworld::HelloReply>(
            cm_, "test_cluster")) {
    ON_CALL(cm_, httpAsyncClientForCluster("test_cluster")).WillByDefault(ReturnRef(http_client_));
  }

  std::unique_ptr<HelloworldStream> createStream(TestMetadata& initial_metadata) {
    std::unique_ptr<HelloworldStream> stream(new HelloworldStream());
    std::vector<Http::LowerCaseString> keys;
    EXPECT_CALL(*stream, onCreateInitialMetadata(_))
        .WillOnce(Invoke([&initial_metadata](Http::HeaderMap& headers) {
          for (auto& value : initial_metadata) {
            headers.addStatic(value.first, value.second);
          }
        }));
    Http::TestHeaderMapImpl headers{{":method", "POST"},
                                    {":path", "/helloworld.Greeter/SayHello"},
                                    {":authority", "test_cluster"},
                                    {"content-type", "application/grpc"}};
    for (auto& value : initial_metadata) {
      headers.addStatic(value.first, value.second);
    }
    EXPECT_CALL(stream->http_stream_, sendHeaders(HeaderMapEqualRef(&headers), _));

    ON_CALL(http_client_, start(_, _))
        .WillByDefault(Invoke([this, &stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                              const Optional<std::chrono::milliseconds>& timeout) {
          UNREFERENCED_PARAMETER(timeout);
          stream->http_callbacks_ = &callbacks;
          return &stream->http_stream_;
        }));
    stream->grpc_stream_ =
        grpc_client_->start(*method_descriptor_, *stream, Optional<std::chrono::milliseconds>());
    EXPECT_NE(stream->grpc_stream_, nullptr);
    return stream;
  }

  const google::protobuf::MethodDescriptor* method_descriptor_;
  NiceMock<Http::MockAsyncClient> http_client_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::unique_ptr<AsyncClientImpl<helloworld::HelloRequest, helloworld::HelloReply>> grpc_client_;
};

// Validate that a simple request-reply stream works.
TEST_F(GrpcAsyncClientImplTest, BasicStream) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  stream->sendServerTrailers(Status::GrpcStatus::Ok, empty_metadata);
  stream->closeStream();
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
  stream_1->sendServerTrailers(Status::GrpcStatus::Unavailable, empty_metadata);
  stream_0->sendServerTrailers(Status::GrpcStatus::Ok, empty_metadata);
  stream_0->closeStream();
}

// Validate that a failure in the HTTP client returns immediately with status
// UNAVAILABLE.
TEST_F(GrpcAsyncClientImplTest, HttpStartFail) {
  MockAsyncClientCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(http_client_, start(_, _)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks, onRemoteClose(Status::GrpcStatus::Unavailable));
  auto* grpc_stream = grpc_client_->start(*method_descriptor_, grpc_callbacks,
                                          Optional<std::chrono::milliseconds>());
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that a non-200 HTTP status results in an INTERNAL gRPC error.
TEST_F(GrpcAsyncClientImplTest, HttpNon200Status) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  Http::HeaderMapPtr reply_headers{new Http::TestHeaderMapImpl{{":status", "404"}}};
  stream->expectGrpcStatus(Status::GrpcStatus::Internal);
  stream->http_callbacks_->onHeaders(std::move(reply_headers), false);
}

// Validate that a HTTP-level reset is handled as an INTERNAL gRPC error.
TEST_F(GrpcAsyncClientImplTest, HttpReset) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->expectGrpcStatus(Status::GrpcStatus::Internal);
  stream->http_callbacks_->onReset();
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
  EXPECT_CALL(*stream, onReceiveTrailingMetadata_(HeaderMapEqualRef(reply_trailers.get())));
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
  EXPECT_CALL(*stream, onReceiveTrailingMetadata_(HeaderMapEqualRef(reply_trailers.get())));
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
  EXPECT_CALL(*stream, onReceiveMessage_(HelloworldReplyEq(HELLO_REPLY)));
  stream->http_callbacks_->onData(reply_buffer, true);
}

// Validate that send client initial metadata works.
TEST_F(GrpcAsyncClientImplTest, ClientInitialMetadata) {
  TestMetadata initial_metadata = {
      {Http::LowerCaseString("foo"), "bar"}, {Http::LowerCaseString("baz"), "blah"},
  };
  createStream(initial_metadata);
}

// Validate that receiving server initial metadata works.
TEST_F(GrpcAsyncClientImplTest, ServerInitialMetadata) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest();
  TestMetadata initial_metadata = {
      {Http::LowerCaseString("foo"), "bar"}, {Http::LowerCaseString("baz"), "blah"},
  };
  stream->sendServerInitialMetadata(initial_metadata);
}

// Validate that receiving server trailing metadata works.
TEST_F(GrpcAsyncClientImplTest, ServerTrailingMetadata) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  TestMetadata trailing_metadata = {
      {Http::LowerCaseString("foo"), "bar"}, {Http::LowerCaseString("baz"), "blah"},
  };
  stream->sendServerTrailers(Status::GrpcStatus::Ok, trailing_metadata);
}

// Validate that a trailers-only response is handled.
TEST_F(GrpcAsyncClientImplTest, TrailersOnly) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendServerTrailers(Status::GrpcStatus::Ok, empty_metadata, true);
  stream->closeStream();
}

// Validate that a trailers RESOURCE_EXHAUSTED reply is handled.
TEST_F(GrpcAsyncClientImplTest, ResourceExhaustedError) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  stream->sendServerTrailers(Status::GrpcStatus::ResourceExhausted, empty_metadata);
}

// Validate that we can continue to receive after a local close.
TEST_F(GrpcAsyncClientImplTest, ReceiveAfterLocalClose) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendRequest();
  stream->closeStream();
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  stream->sendServerTrailers(Status::GrpcStatus::Ok, empty_metadata);
}

// Validate that we can continue to send after a remote close.
TEST_F(GrpcAsyncClientImplTest, SendAfterRemoteClose) {
  TestMetadata empty_metadata;
  auto stream = createStream(empty_metadata);
  stream->sendServerInitialMetadata(empty_metadata);
  stream->sendReply();
  stream->sendServerTrailers(Status::GrpcStatus::Ok, empty_metadata);
  stream->sendRequest();
  stream->closeStream();
}

} // namespace
} // namespace Grpc
} // namespace Envoy
