#include <cstdint>
#include <memory>

#include "test/mocks/server/factory_context.h"

#include "contrib/generic_proxy/filters/network/source/codecs/kafka/config.h"
#include "contrib/generic_proxy/filters/network/test/mocks/codec.h"
#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/external/responses.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Kafka {
namespace {

using testing::NiceMock;

TEST(KafkaCodecTest, SimpleFrameTest) {

  {
    auto request =
        std::make_shared<NetworkFilters::Kafka::Request<NetworkFilters::Kafka::FetchRequest>>(
            NetworkFilters::Kafka::RequestHeader(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0, 3,
                                                 absl::nullopt),
            NetworkFilters::Kafka::FetchRequest({}, {}, {}, {}));

    KafkaRequestFrame frame(request);
    EXPECT_EQ(frame.frameFlags().streamFlags().streamId(), 3);
  }

  {
    KafkaResponseFrame frame(nullptr);
    EXPECT_EQ(frame.protocol(), "kafka");
    EXPECT_EQ(frame.frameFlags().streamFlags().streamId(), 0);
  }

  {
    auto response =
        std::make_shared<NetworkFilters::Kafka::Response<NetworkFilters::Kafka::FetchResponse>>(
            NetworkFilters::Kafka::ResponseMetadata(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0,
                                                    3),
            NetworkFilters::Kafka::FetchResponse({}, {}));

    KafkaResponseFrame frame(response);
    EXPECT_EQ(frame.frameFlags().streamFlags().streamId(), 3);
  }
}

TEST(KafkaCodecTest, KafkaRequestCallbacksTest) {
  NiceMock<GenericProxy::MockServerCodecCallbacks> callbacks;

  KafkaRequestCallbacks request_callbacks(callbacks);

  {
    EXPECT_CALL(callbacks, onDecodingSuccess(_));

    auto request =
        std::make_shared<NetworkFilters::Kafka::Request<NetworkFilters::Kafka::FetchRequest>>(
            NetworkFilters::Kafka::RequestHeader(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0, 3,
                                                 absl::nullopt),
            NetworkFilters::Kafka::FetchRequest({}, {}, {}, {}));

    request_callbacks.onMessage(request);
  }

  {
    EXPECT_CALL(callbacks, onDecodingFailure());
    request_callbacks.onFailedParse(nullptr);
  }
}

TEST(KafkaCodecTest, KafkaResponseCallbacksTest) {
  NiceMock<GenericProxy::MockClientCodecCallbacks> callbacks;

  KafkaResponseCallbacks response_callbacks(callbacks);

  {
    EXPECT_CALL(callbacks, onDecodingSuccess(_));

    auto response =
        std::make_shared<NetworkFilters::Kafka::Response<NetworkFilters::Kafka::FetchResponse>>(
            NetworkFilters::Kafka::ResponseMetadata(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0,
                                                    3),
            NetworkFilters::Kafka::FetchResponse({}, {}));

    response_callbacks.onMessage(response);
  }

  {
    EXPECT_CALL(callbacks, onDecodingFailure());
    response_callbacks.onFailedParse(nullptr);
  }
}

TEST(KafkaCodecTest, KafkaServerCodecTest) {
  NiceMock<GenericProxy::MockServerCodecCallbacks> callbacks;

  KafkaServerCodec server_codec;
  server_codec.setCodecCallbacks(callbacks);

  {
    // Test respond() method.

    auto request =
        std::make_shared<NetworkFilters::Kafka::Request<NetworkFilters::Kafka::FetchRequest>>(
            NetworkFilters::Kafka::RequestHeader(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0, 3,
                                                 absl::nullopt),
            NetworkFilters::Kafka::FetchRequest({}, {}, {}, {}));

    KafkaRequestFrame frame(request);
    auto local_response = server_codec.respond(absl::OkStatus(), "", frame);

    EXPECT_NE(local_response, nullptr);
    EXPECT_EQ(dynamic_cast<KafkaResponseFrame*>(local_response.get())->response_, nullptr);
  }

  {
    // Test decode() method.
    EXPECT_CALL(callbacks, onDecodingSuccess(_))
        .WillOnce(testing::Invoke([](StreamFramePtr request) {
          EXPECT_EQ(dynamic_cast<KafkaRequestFrame*>(request.get())
                        ->request_->request_header_.correlation_id_,
                    3);
        }));

    auto request =
        std::make_shared<NetworkFilters::Kafka::Request<NetworkFilters::Kafka::FetchRequest>>(
            NetworkFilters::Kafka::RequestHeader(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0, 3,
                                                 absl::nullopt),
            NetworkFilters::Kafka::FetchRequest({}, {}, {}, {}));

    Buffer::OwnedImpl buffer;
    const uint32_t size = htobe32(request->computeSize());
    buffer.add(&size, sizeof(size)); // Encode data length.

    request->encode(buffer);
    server_codec.decode(buffer, false);
  }

  {
    // Test encode() method with non-response frame.

    NiceMock<GenericProxy::MockEncodingCallbacks> encoding_callbacks;

    auto request =
        std::make_shared<NetworkFilters::Kafka::Request<NetworkFilters::Kafka::FetchRequest>>(
            NetworkFilters::Kafka::RequestHeader(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0, 3,
                                                 absl::nullopt),
            NetworkFilters::Kafka::FetchRequest({}, {}, {}, {}));
    KafkaRequestFrame request_frame(request);

    // Do nothiing.
    server_codec.encode(request_frame, encoding_callbacks);
  }

  {
    // Test encode() method without actual response.

    NiceMock<GenericProxy::MockEncodingCallbacks> encoding_callbacks;
    NiceMock<Network::MockServerConnection> mock_connection;

    KafkaResponseFrame response_frame(nullptr);

    // Expect close connection.
    EXPECT_CALL(callbacks, connection())
        .WillOnce(testing::Return(makeOptRef<Network::Connection>(mock_connection)));
    EXPECT_CALL(mock_connection, close(Network::ConnectionCloseType::FlushWrite));

    server_codec.encode(response_frame, encoding_callbacks);
  }

  {
    // Test encode() method with response.

    NiceMock<GenericProxy::MockEncodingCallbacks> encoding_callbacks;

    auto response =
        std::make_shared<NetworkFilters::Kafka::Response<NetworkFilters::Kafka::FetchResponse>>(
            NetworkFilters::Kafka::ResponseMetadata(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0,
                                                    3),
            NetworkFilters::Kafka::FetchResponse({}, {}));

    KafkaResponseFrame response_frame(response);

    Envoy::Buffer::OwnedImpl dst_buffer;
    const uint32_t size = htobe32(response->computeSize());
    dst_buffer.add(&size, sizeof(size)); // Encode data length.
    response->encode(dst_buffer);

    EXPECT_CALL(encoding_callbacks, onEncodingSuccess(_, true))
        .WillOnce(testing::Invoke([&](Buffer::Instance& buffer, bool) {
          EXPECT_EQ(buffer.toString(), dst_buffer.toString());
        }));
    server_codec.encode(response_frame, encoding_callbacks);
  }
}

TEST(KafkaCodecTest, KafkaClientCodecTest) {
  NiceMock<GenericProxy::MockClientCodecCallbacks> callbacks;

  KafkaClientCodec client_codec;
  client_codec.setCodecCallbacks(callbacks);

  {
    // Test decode() method.
    EXPECT_CALL(callbacks, onDecodingSuccess(_))
        .WillOnce(testing::Invoke([](StreamFramePtr response) {
          EXPECT_EQ(dynamic_cast<KafkaResponseFrame*>(response.get())
                        ->response_->metadata_.correlation_id_,
                    3);
        }));

    auto response =
        std::make_shared<NetworkFilters::Kafka::Response<NetworkFilters::Kafka::FetchResponse>>(
            NetworkFilters::Kafka::ResponseMetadata(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0,
                                                    3),
            NetworkFilters::Kafka::FetchResponse({}, {}));

    Buffer::OwnedImpl buffer;
    const uint32_t size = htobe32(response->computeSize());
    buffer.add(&size, sizeof(size)); // Encode data length.

    response->encode(buffer);

    client_codec.response_decoder_->expectResponse(3, 0, 0);
    client_codec.decode(buffer, false);
  }

  {
    // Test encode() method with non-request frame.

    NiceMock<GenericProxy::MockEncodingCallbacks> encoding_callbacks;

    auto response =
        std::make_shared<NetworkFilters::Kafka::Response<NetworkFilters::Kafka::FetchResponse>>(
            NetworkFilters::Kafka::ResponseMetadata(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0,
                                                    3),
            NetworkFilters::Kafka::FetchResponse({}, {}));
    KafkaResponseFrame response_frame(response);

    // Do nothiing.
    client_codec.encode(response_frame, encoding_callbacks);
  }

  {
    // Test encode() method with request.

    NiceMock<GenericProxy::MockEncodingCallbacks> encoding_callbacks;

    auto request =
        std::make_shared<NetworkFilters::Kafka::Request<NetworkFilters::Kafka::FetchRequest>>(
            NetworkFilters::Kafka::RequestHeader(NetworkFilters::Kafka::FETCH_REQUEST_API_KEY, 0, 3,
                                                 absl::nullopt),
            NetworkFilters::Kafka::FetchRequest({}, {}, {}, {}));

    KafkaRequestFrame request_frame(request);

    Envoy::Buffer::OwnedImpl dst_buffer;
    const uint32_t size = htobe32(request->computeSize());
    dst_buffer.add(&size, sizeof(size)); // Encode data length.
    request->encode(dst_buffer);

    EXPECT_CALL(encoding_callbacks, onEncodingSuccess(_, true))
        .WillOnce(testing::Invoke([&](Buffer::Instance& buffer, bool) {
          EXPECT_EQ(buffer.toString(), dst_buffer.toString());
        }));

    client_codec.encode(request_frame, encoding_callbacks);
  }
}

} // namespace
} // namespace Kafka
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
