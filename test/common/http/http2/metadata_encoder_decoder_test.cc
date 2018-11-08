#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/http/http2/metadata_decoder.h"
#include "common/http/http2/metadata_encoder.h"
#include "common/runtime/runtime_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nghttp2/nghttp2.h"

// A global variable in nghttp2 to disable preface and initial settings for tests.
// TODO(soya3129): Remove after issue https://github.com/nghttp2/nghttp2/issues/1246 is fixed.
extern int nghttp2_enable_strict_preface;

namespace Envoy {
namespace Http {
namespace Http2 {

namespace {
static const uint64_t STREAM_ID = 1;

// The buffer stores data sent by encoder and received by decoder.
typedef struct {
  uint8_t buf[1024 * 1024] = {0};
  size_t length = 0;
} TestBuffer;

// The application data structure passes to nghttp2 session.
typedef struct {
  MetadataEncoder* encoder;
  MetadataDecoder* decoder;
  // Stores data sent by encoder and received by the decoder.
  TestBuffer* output_buffer;
} UserData;

// Nghttp2 callback function for sending extension frame.
static ssize_t pack_extension_callback(nghttp2_session* session, uint8_t* buf, size_t len,
                                       const nghttp2_frame* frame, void* user_data) {
  EXPECT_NE(nullptr, session);

  // Computes the size to pack.
  MetadataEncoder* encoder = reinterpret_cast<UserData*>(user_data)->encoder;
  const uint64_t size_from_encoder =
      std::min(METADATA_MAX_PAYLOAD_SIZE, encoder->payload().length());
  const uint64_t size_to_copy = std::min(static_cast<uint64_t>(len), size_from_encoder);

  Buffer::OwnedImpl& p = encoder->payload();
  p.copyOut(0, size_to_copy, buf);

  // Releases the payload that has been copied to nghttp2.
  encoder->releasePayload(size_to_copy);

  // Keep submitting extension frames if there is payload left in the encoder.
  if (encoder->hasNextFrame()) {
    const uint8_t flag =
        (encoder->payload().length() > METADATA_MAX_PAYLOAD_SIZE) ? 0 : END_METADATA_FLAG;
    int result =
        nghttp2_submit_extension(session, METADATA_FRAME_TYPE, flag, frame->hd.stream_id, nullptr);
    EXPECT_EQ(0, result);
  }

  return static_cast<ssize_t>(size_to_copy);
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

class MetadataEncoderDecoderTest : public ::testing::Test {
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

  void verifyMetadata(MetadataMap& expect, std::unique_ptr<MetadataMap> metadata_map) {
    EXPECT_EQ(metadata_map->size(), expect.size());
    for (const auto& metadata : *metadata_map) {
      EXPECT_EQ(expect.find(metadata.first)->second, metadata.second);
    }
  }

  nghttp2_session* session_ = nullptr;
  nghttp2_session_callbacks* callbacks_;
  MetadataEncoder encoder_;
  std::unique_ptr<MetadataDecoder> decoder_;
  nghttp2_option* option_;

  // Stores data received by peer.
  TestBuffer output_buffer_;

  // Application data passed to nghttp2.
  UserData user_data_;

  Runtime::RandomGeneratorImpl random_generator_;
};

// Tests encoding and decoding small METADATAs, which can fit in one HTTP2 frame.
TEST_F(MetadataEncoderDecoderTest, EncodeDecodeSmallHeaderBlock) {
  MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
      {"header_key2", "header_value2"},
      {"header_key3", "header_value3"},
      {"header_key4", "header_value4"},
  };

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderDecoderTest::verifyMetadata, this, metadata_map,
                                  std::placeholders::_1);
  initialize(cb);

  // Creates metadata payload.
  encoder_.createPayload(metadata_map);
  // Submits METADATA to nghttp2.
  int result = nghttp2_submit_extension(session_, METADATA_FRAME_TYPE, END_METADATA_FLAG, STREAM_ID,
                                        nullptr);
  EXPECT_EQ(0, result);
  // Sends METADATA to nghttp2.
  result = nghttp2_session_send(session_);
  EXPECT_EQ(0, result);

  // Runs decoder and verifies the decoded payload.
  const uint64_t consume_size = random_generator_.random() % output_buffer_.length;
  result = nghttp2_session_mem_recv(session_, output_buffer_.buf, consume_size);
  result = nghttp2_session_mem_recv(session_, output_buffer_.buf + consume_size,
                                    output_buffer_.length - consume_size);

  cleanUp();
}

// Tests encoding/decoding large METADATAs, which can cross multiple HTTP2 frames.
TEST_F(MetadataEncoderDecoderTest, EncodeLargeHeaderBlock) {
  MetadataMap metadata_map = {
      {"header_key1", std::string(50000, 'a')},
      {"header_key2", std::string(50000, 'b')},
  };

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderDecoderTest::verifyMetadata, this, metadata_map,
                                  std::placeholders::_1);
  initialize(cb);

  // Creates metadata payload.
  encoder_.createPayload(metadata_map);

  // Submits METADATA to nghttp2.
  const uint8_t flag =
      (encoder_.payload().length() > METADATA_MAX_PAYLOAD_SIZE) ? 0 : END_METADATA_FLAG;
  // The payload can't be submitted within one frame. Callback function will keep submitting until
  // all the payload has been submitted.
  int result = nghttp2_submit_extension(session_, METADATA_FRAME_TYPE, flag, STREAM_ID, nullptr);
  EXPECT_EQ(0, result);

  // Sends METADATA to nghttp2.
  result = nghttp2_session_send(session_);
  EXPECT_EQ(0, result);

  // Verifies flag and payload are encoded correctly.
  const uint64_t consume_size = random_generator_.random() % output_buffer_.length;
  result = nghttp2_session_mem_recv(session_, output_buffer_.buf, consume_size);
  result = nghttp2_session_mem_recv(session_, output_buffer_.buf + consume_size,
                                    output_buffer_.length - consume_size);

  cleanUp();
}

// Tests encoder and decoder can perform on multiple MetadataMaps.
TEST_F(MetadataEncoderDecoderTest, VerifyEncoderDecoderOnMultipleMetadataMaps) {
  MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
      {"header_key2", "header_value2"},
      {"header_key3", "header_value3"},
      {"header_key4", "header_value4"},
  };

  // Encode and decode the first MetadataMap.
  MetadataCallback cb = std::bind(&MetadataEncoderDecoderTest::verifyMetadata, this, metadata_map,
                                  std::placeholders::_1);
  initialize(cb);

  encoder_.createPayload(metadata_map);
  nghttp2_submit_extension(session_, METADATA_FRAME_TYPE, END_METADATA_FLAG, STREAM_ID, nullptr);
  nghttp2_session_send(session_);
  nghttp2_session_mem_recv(session_, output_buffer_.buf, output_buffer_.length);

  // Cleans up the output buffer.
  memset(output_buffer_.buf, 0, output_buffer_.length);
  output_buffer_.length = 0;

  MetadataMap metadata_map_2 = {
      {"header_key4", "header_value4"},
      {"header_key5", "header_value5"},
      {"header_key6", "header_value6"},
      {"header_key7", "header_value7"},
  };

  // Encode and decode the second MetadataMap.
  MetadataCallback cb2 = std::bind(&MetadataEncoderDecoderTest::verifyMetadata, this,
                                   metadata_map_2, std::placeholders::_1);
  decoder_->callback_ = cb2;
  encoder_.createPayload(metadata_map_2);
  nghttp2_submit_extension(session_, METADATA_FRAME_TYPE, END_METADATA_FLAG, STREAM_ID, nullptr);
  nghttp2_session_send(session_);
  nghttp2_session_mem_recv(session_, output_buffer_.buf, output_buffer_.length);

  cleanUp();
}

TEST_F(MetadataEncoderDecoderTest, TestMetadataSizeLimit) {
  MetadataMap metadata_map = {
      {"header_key1", std::string(1024 * 1024 + 1, 'a')},
  };

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderDecoderTest::verifyMetadata, this, metadata_map,
                                  std::placeholders::_1);
  initialize(cb);

  // metadata_map exceeds size limit.
  EXPECT_FALSE(encoder_.createPayload(metadata_map));

  std::string payload = std::string(1024 * 1024 + 1, 'a');
  EXPECT_FALSE(
      decoder_->receiveMetadata(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));

  cleanUp();
}

TEST_F(MetadataEncoderDecoderTest, TestDecodeBadData) {
  MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
  };

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderDecoderTest::verifyMetadata, this, metadata_map,
                                  std::placeholders::_1);
  initialize(cb);

  // Generates payload.
  encoder_.createPayload(metadata_map);
  nghttp2_submit_extension(session_, METADATA_FRAME_TYPE, END_METADATA_FLAG, STREAM_ID, nullptr);
  nghttp2_session_send(session_);

  // Messes up with the encoded payload, and passes it to the decoder.
  output_buffer_.buf[10] |= 0xff;
  decoder_->receiveMetadata(output_buffer_.buf, output_buffer_.length);
  EXPECT_FALSE(decoder_->onMetadataFrameComplete(true));

  cleanUp();
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
