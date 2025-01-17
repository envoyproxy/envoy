#include "envoy/http/metadata_interface.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/common/random_generator.h"
#include "source/common/http/http2/metadata_decoder.h"
#include "source/common/http/http2/metadata_encoder.h"

#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "http2_frame.h"
#include "quiche/http2/adapter/data_source.h"
#include "quiche/http2/adapter/mock_http2_visitor.h"
#include "quiche/http2/adapter/nghttp2_adapter.h"

// A global variable in nghttp2 to disable preface and initial settings for tests.
// TODO(soya3129): Remove after issue https://github.com/nghttp2/nghttp2/issues/1246 is fixed.
extern "C" {
extern int nghttp2_enable_strict_preface;
}

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

absl::string_view toStringView(uint8_t* data, size_t length) {
  return {reinterpret_cast<char*>(data), length};
}

static const uint64_t STREAM_ID = 1;

// The buffer stores data sent by encoder and received by decoder.
struct TestBuffer {
  uint8_t buf[1024 * 1024] = {0};
  size_t length = 0;
};

class MetadataUnpackingVisitor : public http2::adapter::test::MockHttp2Visitor {
public:
  MetadataUnpackingVisitor(MetadataDecoder* decoder, TestBuffer* output_buffer)
      : decoder_(decoder), buffer_(output_buffer) {
    // These calls are not important to intercept for these tests.
    EXPECT_CALL(*this, OnBeforeFrameSent).Times(testing::AnyNumber());
    EXPECT_CALL(*this, OnFrameSent).Times(testing::AnyNumber());
    EXPECT_CALL(*this, OnFrameHeader).Times(testing::AnyNumber());
  }

  void OnBeginMetadataForStream(http2::adapter::Http2StreamId /*stream_id*/,
                                size_t payload_length) override {
    remaining_payload_ = payload_length;
  }

  bool OnMetadataForStream(http2::adapter::Http2StreamId /*stream_id*/,
                           absl::string_view metadata) override {
    return decoder_->receiveMetadata(reinterpret_cast<const uint8_t*>(metadata.data()),
                                     metadata.size());
  }

  bool OnMetadataEndForStream(http2::adapter::Http2StreamId /*stream_id*/) override {
    return decoder_->onMetadataFrameComplete(true);
  }

  int64_t OnReadyToSend(absl::string_view serialized) override {
    memcpy(buffer_->buf + buffer_->length, serialized.data(), serialized.size());
    buffer_->length += serialized.size();
    return serialized.size();
  }

  MetadataDecoder* decoder_;
  TestBuffer* buffer_;
  size_t remaining_payload_ = 0;
};

} // namespace

class MetadataEncoderTest : public testing::Test {
public:
  void initialize(MetadataCallback cb) {
    decoder_ = std::make_unique<MetadataDecoder>(cb);

    // Enables extension frame.
    nghttp2_option* option;
    nghttp2_option_new(&option);
    nghttp2_option_set_user_recv_extension_type(option, METADATA_FRAME_TYPE);

    // Creates new nghttp2 session.
    nghttp2_enable_strict_preface = 0;
    visitor_ = std::make_unique<MetadataUnpackingVisitor>(decoder_.get(), &output_buffer_);

    session_ = http2::adapter::NgHttp2Adapter::CreateClientAdapter(*visitor_, option);
    nghttp2_enable_strict_preface = 1;
    nghttp2_option_del(option);
  }

  void verifyMetadataMapVector(MetadataMapVector& expect, MetadataMapPtr&& metadata_map_ptr) {
    for (const auto& metadata : *metadata_map_ptr) {
      EXPECT_EQ(expect.front()->find(metadata.first)->second, metadata.second);
    }
    expect.erase(expect.begin());
  }

  void submitMetadata(const MetadataMapVector& metadata_map_vector) {
    // Creates metadata payload.
    NewMetadataEncoder::MetadataSourceVector sources = encoder_.createSources(metadata_map_vector);
    for (auto& source : sources) {
      session_->SubmitMetadata(STREAM_ID, 16 * 1024, std::move(source));
    }
    // Triggers nghttp2 to populate the payloads of the METADATA frames.
    int result = session_->Send();
    EXPECT_EQ(0, result);
  }

  std::unique_ptr<http2::adapter::NgHttp2Adapter> session_;
  std::unique_ptr<MetadataUnpackingVisitor> visitor_;
  NewMetadataEncoder encoder_;
  std::unique_ptr<MetadataDecoder> decoder_;
  int count_ = 0;

  // Stores data received by peer.
  TestBuffer output_buffer_;

  Random::RandomGeneratorImpl random_generator_;
};

TEST_F(MetadataEncoderTest, TestTotalPayloadSize) {
  initialize([](MetadataMapPtr&&) {});

  const std::string payload = std::string(1024, 'a');
  EXPECT_EQ(0, decoder_->totalPayloadSize());
  EXPECT_TRUE(
      decoder_->receiveMetadata(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
  EXPECT_EQ(payload.size(), decoder_->totalPayloadSize());
  EXPECT_TRUE(
      decoder_->receiveMetadata(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
  EXPECT_EQ(2 * payload.size(), decoder_->totalPayloadSize());
}

TEST_F(MetadataEncoderTest, TestDecodeBadData) {
  MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
  };
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));

  // Verifies the encoding/decoding result in decoder's callback functions.
  initialize([this, &metadata_map_vector](MetadataMapPtr&& metadata_map_ptr) -> void {
    this->verifyMetadataMapVector(metadata_map_vector, std::move(metadata_map_ptr));
  });
  submitMetadata(metadata_map_vector);

  // Messes up with the encoded payload, and passes it to the decoder.
  output_buffer_.buf[10] |= 0xff;
  decoder_->receiveMetadata(output_buffer_.buf, output_buffer_.length);
  EXPECT_FALSE(decoder_->onMetadataFrameComplete(true));
}

// Checks if accumulated metadata size reaches size limit, returns failure.
TEST_F(MetadataEncoderTest, VerifyEncoderDecoderMultipleMetadataReachSizeLimit) {
  MetadataMap metadata_map_empty = {};
  MetadataCallback cb = [](std::unique_ptr<MetadataMap>) -> void {};
  initialize(cb);

  ssize_t result = 0;

  EXPECT_CALL(*visitor_, OnConnectionError);
  for (int i = 0; i < 100; i++) {
    // Cleans up the output buffer.
    memset(output_buffer_.buf, 0, output_buffer_.length);
    output_buffer_.length = 0;

    MetadataMap metadata_map = {
        {"header_key1", std::string(10000, 'a')},
        {"header_key2", std::string(10000, 'b')},
    };
    MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
    MetadataMapVector metadata_map_vector;
    metadata_map_vector.push_back(std::move(metadata_map_ptr));

    // Encode and decode the second MetadataMap.
    decoder_->callback_ = [this, &metadata_map_vector](MetadataMapPtr&& metadata_map_ptr) -> void {
      this->verifyMetadataMapVector(metadata_map_vector, std::move(metadata_map_ptr));
    };
    submitMetadata(metadata_map_vector);

    result = session_->ProcessBytes(toStringView(output_buffer_.buf, output_buffer_.length));
    if (result < 0) {
      break;
    }
  }
  // Verifies max metadata limit reached.
  EXPECT_LT(result, 0);
  EXPECT_LE(decoder_->max_payload_size_bound_, decoder_->total_payload_size_);
}

// Tests encoding an empty map.
TEST_F(MetadataEncoderTest, EncodeMetadataMapEmpty) {
  MetadataMap empty = {};
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(empty);

  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));

  // Verifies the encoding/decoding result in decoder's callback functions.
  initialize([this, &metadata_map_vector](MetadataMapPtr&& metadata_map_ptr) -> void {
    this->verifyMetadataMapVector(metadata_map_vector, std::move(metadata_map_ptr));
  });
  submitMetadata(metadata_map_vector);

  // The empty metadata map is ignored, and does not result in any frames being emitted.
  EXPECT_EQ(output_buffer_.length, 0);
}

// Tests encoding/decoding small metadata map vectors.
TEST_F(MetadataEncoderTest, EncodeMetadataMapVectorSmall) {
  MetadataMap metadata_map = {
      {"header_key1", std::string(5, 'a')},
      {"header_key2", std::string(5, 'b')},
  };
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
  MetadataMap metadata_map_2 = {
      {"header_key3", std::string(5, 'a')},
      {"header_key4", std::string(5, 'b')},
  };
  MetadataMapPtr metadata_map_ptr_2 = std::make_unique<MetadataMap>(metadata_map);
  MetadataMap metadata_map_3 = {
      {"header_key1", std::string(1, 'a')},
      {"header_key2", std::string(1, 'b')},
  };
  MetadataMapPtr metadata_map_ptr_3 = std::make_unique<MetadataMap>(metadata_map);

  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  metadata_map_vector.push_back(std::move(metadata_map_ptr_2));
  metadata_map_vector.push_back(std::move(metadata_map_ptr_3));

  // Verifies the encoding/decoding result in decoder's callback functions.
  initialize([this, &metadata_map_vector](MetadataMapPtr&& metadata_map_ptr) -> void {
    this->verifyMetadataMapVector(metadata_map_vector, std::move(metadata_map_ptr));
  });
  submitMetadata(metadata_map_vector);

  // Verifies flag and payload are encoded correctly.
  const uint64_t consume_size = random_generator_.random() % output_buffer_.length;
  session_->ProcessBytes(toStringView(output_buffer_.buf, consume_size));
  session_->ProcessBytes(
      toStringView(output_buffer_.buf + consume_size, output_buffer_.length - consume_size));
}

// Tests encoding/decoding large metadata map vectors.
TEST_F(MetadataEncoderTest, EncodeMetadataMapVectorLarge) {
  MetadataMapVector metadata_map_vector;
  for (int i = 0; i < 10; i++) {
    MetadataMap metadata_map = {
        {"header_key1", std::string(50000, 'a')},
        {"header_key2", std::string(50000, 'b')},
    };
    MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
    metadata_map_vector.push_back(std::move(metadata_map_ptr));
  }
  // Verifies the encoding/decoding result in decoder's callback functions.
  initialize([this, &metadata_map_vector](MetadataMapPtr&& metadata_map_ptr) -> void {
    this->verifyMetadataMapVector(metadata_map_vector, std::move(metadata_map_ptr));
  });
  submitMetadata(metadata_map_vector);
  // Verifies flag and payload are encoded correctly.
  const uint64_t consume_size = random_generator_.random() % output_buffer_.length;
  session_->ProcessBytes(toStringView(output_buffer_.buf, consume_size));
  session_->ProcessBytes(
      toStringView(output_buffer_.buf + consume_size, output_buffer_.length - consume_size));
}

// Tests encoding/decoding with fuzzed metadata size.
TEST_F(MetadataEncoderTest, EncodeFuzzedMetadata) {
  MetadataMapVector metadata_map_vector;
  for (int i = 0; i < 10; i++) {
    Random::RandomGeneratorImpl random;
    int value_size_1 = random.random() % (2 * Http::METADATA_MAX_PAYLOAD_SIZE) + 1;
    int value_size_2 = random.random() % (2 * Http::METADATA_MAX_PAYLOAD_SIZE) + 1;
    MetadataMap metadata_map = {
        {"header_key1", std::string(value_size_1, 'a')},
        {"header_key2", std::string(value_size_2, 'a')},
    };
    MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
    metadata_map_vector.push_back(std::move(metadata_map_ptr));
  }

  // Verifies the encoding/decoding result in decoder's callback functions.
  initialize([this, &metadata_map_vector](MetadataMapPtr&& metadata_map_ptr) -> void {
    this->verifyMetadataMapVector(metadata_map_vector, std::move(metadata_map_ptr));
  });
  submitMetadata(metadata_map_vector);

  // Verifies flag and payload are encoded correctly.
  session_->ProcessBytes(toStringView(output_buffer_.buf, output_buffer_.length));
}

TEST_F(MetadataEncoderTest, EncodeDecodeFrameTest) {
  MetadataMap metadataMap = {
      {"Connections", "15"},
      {"Timeout Seconds", "10"},
  };
  MetadataMapPtr metadataMapPtr = std::make_unique<MetadataMap>(metadataMap);
  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadataMapPtr));
  Http2Frame http2FrameFromUltility = Http2Frame::makeMetadataFrameFromMetadataMap(
      1, metadataMap, Http2Frame::MetadataFlags::EndMetadata);
  MetadataDecoder decoder([this, &metadata_map_vector](MetadataMapPtr&& metadata_map_ptr) -> void {
    this->verifyMetadataMapVector(metadata_map_vector, std::move(metadata_map_ptr));
  });
  decoder.receiveMetadata(http2FrameFromUltility.data() + 9, http2FrameFromUltility.size() - 9);
  decoder.onMetadataFrameComplete(true);
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
