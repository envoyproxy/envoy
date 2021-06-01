#include "envoy/http/metadata_interface.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/common/random_generator.h"
#include "common/http/http2/metadata_decoder.h"
#include "common/http/http2/metadata_encoder.h"

#include "test/test_common/logging.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "http2_frame.h"
#include "nghttp2/nghttp2.h"

// A global variable in nghttp2 to disable preface and initial settings for tests.
// TODO(soya3129): Remove after issue https://github.com/nghttp2/nghttp2/issues/1246 is fixed.
extern "C" {
extern int nghttp2_enable_strict_preface;
}

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

static const uint64_t STREAM_ID = 1;

// The buffer stores data sent by encoder and received by decoder.
struct TestBuffer {
  uint8_t buf[1024 * 1024] = {0};
  size_t length = 0;
};

// The application data structure passes to nghttp2 session.
struct UserData {
  MetadataEncoder* encoder;
  MetadataDecoder* decoder;
  // Stores data sent by encoder and received by the decoder.
  TestBuffer* output_buffer;
};

// Nghttp2 callback function for sending extension frame.
static ssize_t pack_extension_callback(nghttp2_session* session, uint8_t* buf, size_t len,
                                       const nghttp2_frame*, void* user_data) {
  EXPECT_NE(nullptr, session);

  MetadataEncoder* encoder = reinterpret_cast<UserData*>(user_data)->encoder;
  const uint64_t size_copied = encoder->packNextFramePayload(buf, len);

  return static_cast<ssize_t>(size_copied);
}

// Nghttp2 callback function for receiving extension frame.
static int on_extension_chunk_recv_callback(nghttp2_session* session, const nghttp2_frame_hd* hd,
                                            const uint8_t* data, size_t len, void* user_data) {
  EXPECT_NE(nullptr, session);
  EXPECT_GE(hd->length, len);

  MetadataDecoder* decoder = reinterpret_cast<UserData*>(user_data)->decoder;
  bool success = decoder->receiveMetadata(data, len);
  return success ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

// Nghttp2 callback function for unpack extension frames.
static int unpack_extension_callback(nghttp2_session* session, void** payload,
                                     const nghttp2_frame_hd* hd, void* user_data) {
  EXPECT_NE(nullptr, session);
  EXPECT_NE(nullptr, hd);
  EXPECT_NE(nullptr, payload);

  MetadataDecoder* decoder = reinterpret_cast<UserData*>(user_data)->decoder;
  bool result = decoder->onMetadataFrameComplete((hd->flags == END_METADATA_FLAG) ? true : false);
  return result ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

// Nghttp2 callback function for sending data to peer.
static ssize_t send_callback(nghttp2_session* session, const uint8_t* buf, size_t len, int flags,
                             void* user_data) {
  EXPECT_NE(nullptr, session);
  EXPECT_LE(0, flags);

  TestBuffer* buffer = (reinterpret_cast<UserData*>(user_data))->output_buffer;
  memcpy(buffer->buf + buffer->length, buf, len);
  buffer->length += len;
  return len;
}

} // namespace

class MetadataEncoderDecoderTest : public testing::Test {
public:
  void initialize(MetadataCallback cb) {
    decoder_ = std::make_unique<MetadataDecoder>(cb);

    // Enables extension frame.
    nghttp2_option_new(&option_);
    nghttp2_option_set_user_recv_extension_type(option_, METADATA_FRAME_TYPE);

    // Sets callback functions.
    nghttp2_session_callbacks_new(&callbacks_);
    nghttp2_session_callbacks_set_pack_extension_callback(callbacks_, pack_extension_callback);
    nghttp2_session_callbacks_set_send_callback(callbacks_, send_callback);
    nghttp2_session_callbacks_set_on_extension_chunk_recv_callback(
        callbacks_, on_extension_chunk_recv_callback);
    nghttp2_session_callbacks_set_unpack_extension_callback(callbacks_, unpack_extension_callback);

    // Sets application data to pass to nghttp2 session.
    user_data_.encoder = &encoder_;
    user_data_.decoder = decoder_.get();
    user_data_.output_buffer = &output_buffer_;

    // Creates new nghttp2 session.
    nghttp2_enable_strict_preface = 0;
    nghttp2_session_client_new2(&session_, callbacks_, &user_data_, option_);
    nghttp2_enable_strict_preface = 1;
  }

  void cleanUp() {
    nghttp2_session_del(session_);
    nghttp2_session_callbacks_del(callbacks_);
    nghttp2_option_del(option_);
  }

  void verifyMetadataMapVector(MetadataMapVector& expect, MetadataMapPtr&& metadata_map_ptr) {
    for (const auto& metadata : *metadata_map_ptr) {
      EXPECT_EQ(expect.front()->find(metadata.first)->second, metadata.second);
    }
    expect.erase(expect.begin());
  }

  void submitMetadata(const MetadataMapVector& metadata_map_vector) {
    // Creates metadata payload.
    encoder_.createPayload(metadata_map_vector);
    for (uint8_t flags : encoder_.payloadFrameFlagBytes()) {
      int result =
          nghttp2_submit_extension(session_, METADATA_FRAME_TYPE, flags, STREAM_ID, nullptr);
      EXPECT_EQ(0, result);
    }
    // Triggers nghttp2 to populate the payloads of the METADATA frames.
    int result = nghttp2_session_send(session_);
    EXPECT_EQ(0, result);
  }

  nghttp2_session* session_ = nullptr;
  nghttp2_session_callbacks* callbacks_;
  MetadataEncoder encoder_;
  std::unique_ptr<MetadataDecoder> decoder_;
  nghttp2_option* option_;
  int count_ = 0;

  // Stores data received by peer.
  TestBuffer output_buffer_;

  // Application data passed to nghttp2.
  UserData user_data_;

  Random::RandomGeneratorImpl random_generator_;
};

TEST_F(MetadataEncoderDecoderTest, TestMetadataSizeLimit) {
  MetadataMap metadata_map = {
      {"header_key1", std::string(1024 * 1024 + 1, 'a')},
  };
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));

  // Verifies the encoding/decoding result in decoder's callback functions.
  initialize([this, &metadata_map_vector](MetadataMapPtr&& metadata_map_ptr) -> void {
    this->verifyMetadataMapVector(metadata_map_vector, std::move(metadata_map_ptr));
  });

  // metadata_map exceeds size limit.
  EXPECT_LOG_CONTAINS("error", "exceeds the max bound.",
                      EXPECT_FALSE(encoder_.createPayload(metadata_map_vector)));

  std::string payload = std::string(1024 * 1024 + 1, 'a');
  EXPECT_FALSE(
      decoder_->receiveMetadata(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));

  cleanUp();
}

TEST_F(MetadataEncoderDecoderTest, TestDecodeBadData) {
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

  cleanUp();
}

// Checks if accumulated metadata size reaches size limit, returns failure.
TEST_F(MetadataEncoderDecoderTest, VerifyEncoderDecoderMultipleMetadataReachSizeLimit) {
  MetadataMap metadata_map_empty = {};
  MetadataCallback cb = [](std::unique_ptr<MetadataMap>) -> void {};
  initialize(cb);

  ssize_t result = 0;

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

    result = nghttp2_session_mem_recv(session_, output_buffer_.buf, output_buffer_.length);
    if (result < 0) {
      break;
    }
  }
  // Verifies max metadata limit reached.
  EXPECT_LT(result, 0);
  EXPECT_LE(decoder_->max_payload_size_bound_, decoder_->total_payload_size_);

  cleanUp();
}

// Tests encoding/decoding small metadata map vectors.
TEST_F(MetadataEncoderDecoderTest, EncodeMetadataMapVectorSmall) {
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
  nghttp2_session_mem_recv(session_, output_buffer_.buf, consume_size);
  nghttp2_session_mem_recv(session_, output_buffer_.buf + consume_size,
                           output_buffer_.length - consume_size);

  cleanUp();
}

// Tests encoding/decoding large metadata map vectors.
TEST_F(MetadataEncoderDecoderTest, EncodeMetadataMapVectorLarge) {
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
  nghttp2_session_mem_recv(session_, output_buffer_.buf, consume_size);
  nghttp2_session_mem_recv(session_, output_buffer_.buf + consume_size,
                           output_buffer_.length - consume_size);
  cleanUp();
}

// Tests encoding/decoding with fuzzed metadata size.
TEST_F(MetadataEncoderDecoderTest, EncodeFuzzedMetadata) {
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
  nghttp2_session_mem_recv(session_, output_buffer_.buf, output_buffer_.length);

  cleanUp();
}

TEST_F(MetadataEncoderDecoderTest, EncodeDecodeFrameTest) {
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

using MetadataEncoderDecoderDeathTest = MetadataEncoderDecoderTest;

// Crash if a caller tries to pack more frames than the encoder has data for.
TEST_F(MetadataEncoderDecoderDeathTest, PackTooManyFrames) {
  MetadataMap metadata_map = {
      {"header_key1", std::string(5, 'a')},
      {"header_key2", std::string(5, 'b')},
  };
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));

  initialize([this, &metadata_map_vector](MetadataMapPtr&& metadata_map_ptr) -> void {
    this->verifyMetadataMapVector(metadata_map_vector, std::move(metadata_map_ptr));
  });
  submitMetadata(metadata_map_vector);

  // Try to send an extra METADATA frame. Submitting the frame to nghttp2 should succeed, but
  // pack_extension_callback should fail, and that failure will propagate through
  // nghttp2_session_send. How to handle the failure is up to the HTTP/2 codec (in practice, it will
  // throw a CodecProtocolException).
  int result = nghttp2_submit_extension(session_, METADATA_FRAME_TYPE, 0, STREAM_ID, nullptr);
  EXPECT_EQ(0, result);
  EXPECT_DEATH(nghttp2_session_send(session_),
               "No payload remaining to pack into a METADATA frame.");

  cleanUp();
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
