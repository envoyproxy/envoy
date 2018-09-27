#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/http/http2/metadata_decoder.h"
#include "common/http/http2/metadata_encoder.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nghttp2/nghttp2.h"

// A global variable in nghttp2 to disable preface and initial settings for
// tests.
extern int nghttp2_enable_strict_preface;

namespace Envoy {
namespace Http {
namespace Http2 {

namespace {
static const uint64_t STREAM_ID = 1;

// The buffer stores data sent by nghttp2 session to its peer.
typedef struct {
  uint8_t buf[1024 * 1024] = {0};
  size_t length = 0;
} TestBuffer;

// The application data structure passes to nghttp2 session.
typedef struct {
  MetadataEncoder* encoder;
  MetadataDecoder* decoder;
  // Stores data received by the peer.
  TestBuffer* output_buffer;
  // Stores inflated extension frame payload received by the peer.
  TestBuffer* payload_buffer;
  //don't need flag anymore+++++++++++++++++++++++++++++
  uint64_t flag = 0;
} UserData;

// Nghttp2 callback function for sending extension frame.
static ssize_t pack_extension_callback(nghttp2_session* session, uint8_t* buf, size_t len,
                                       const nghttp2_frame* frame, void* user_data) {
  EXPECT_NE(nullptr, session);
  EXPECT_EQ(STREAM_ID, frame->hd.stream_id);

  MetadataEncoder* encoder = reinterpret_cast<UserData*>(user_data)->encoder;
  const uint64_t size_from_encoder =
      std::min(encoder->getMaxMetadataSize(), encoder->payload().length());
  const uint64_t size_to_copy = std::min(static_cast<uint64_t>(len), size_from_encoder);
  Buffer::OwnedImpl& p = reinterpret_cast<Buffer::OwnedImpl&>(encoder->payload());
  p.copyOut(0, size_to_copy, buf);

  // Releases the payload that has been copied to nghttp2.
  encoder->releasePayload(size_to_copy);

  // Keep submitting extension frames if there is payload left in the encoder.
  if (encoder->hasNextFrame()) {
    const uint8_t flag =
        (encoder->payload().length() > encoder->getMaxMetadataSize()) ? 0 : END_METADATA_FLAG;
    int result = nghttp2_submit_extension(session, METADATA_FRAME_TYPE, flag, frame->hd.stream_id,
                                          &encoder->payload());
    EXPECT_EQ(0, result);
  }

  return static_cast<ssize_t>(size_to_copy);
}

// Nghttp2 callback function for receiving extension frame.
static int on_extension_chunk_recv_callback(nghttp2_session* session, const nghttp2_frame_hd* hd,
                                            const uint8_t* data, size_t len, void* user_data) {
  EXPECT_NE(nullptr, session);

  // Sanity check header length.
  EXPECT_EQ(hd->length, len);
  // All the flag should be 0 except the last frame.
  EXPECT_EQ(0, reinterpret_cast<UserData*>(user_data)->flag);
  reinterpret_cast<UserData*>(user_data)->flag = hd->flags;

  MetadataDecoder* decoder = reinterpret_cast<UserData*>(user_data)->decoder;
  decoder->receiveMetadata(data, len);
  return 0;

  //TestBuffer* payload = (reinterpret_cast<UserData*>(user_data))->payload_buffer;
  //ENVOY_LOG_MISC(error, "++++++++++++++payload.length:  {}", payload->length);
  //memcpy(payload->buf + payload->length, data, len);
  //payload->length += len;
  //return 0;
}

// Nghttp2 callback function for unpack extension frames.
static int unpack_extension_callback(nghttp2_session* session, void** payload,
                                     const nghttp2_frame_hd* hd, void* user_data) {
  EXPECT_NE(nullptr, session);
  EXPECT_NE(nullptr, hd);
  TestBuffer* output = reinterpret_cast<UserData*>(user_data)->output_buffer;
  *payload = output;

  MetadataDecoder* decoder = reinterpret_cast<UserData*>(user_data)->decoder;
  bool result = decoder->OnMetadataFrameComplete((hd->flags == END_METADATA_FLAG) ? 1 : 0);
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

class MetadataEncoderTest : public ::testing::Test {
public:
  MetadataEncoderTest() : encoder_(STREAM_ID), decoder_(STREAM_ID) {}

  void initialize() {
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
    user_data_.decoder = &decoder_;
    user_data_.output_buffer = &output_buffer_;
    user_data_.payload_buffer = &payload_buffer_;

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

  void verifyMetadata(MetadataMap* expect, const MetadataMap& metadata_map) {
    ENVOY_LOG_MISC(error, "++++++++++++ callback triggered");
    EXPECT_EQ(metadata_map.size(), expect->size());
    for (const auto& metadata : metadata_map) {
      ENVOY_LOG_MISC(error, "++++++++++++ metadata.first: {}, metadata_second: {}",
                     metadata.first, metadata.second);
      EXPECT_EQ(expect->find(metadata.first)->second, metadata.second);
    }
  }

  // Verifies the payload received by peer is as expected.
  void verifyPayload(MetadataMap& metadata_map, uint8_t* in, size_t inlen) {
    // Creates a new inflater (decoder).
    nghttp2_hd_inflater* inflater;
    int rv = nghttp2_hd_inflate_new(&inflater);
    if (rv != 0) {
      ENVOY_LOG_MISC(error, "Fails to create inflater.");
    }

    for (const auto& header : metadata_map) {
      nghttp2_nv nv;
      int inflate_flags = 0;

      // Decodes header block.
      ssize_t result = nghttp2_hd_inflate_hd(inflater, &nv, &inflate_flags, in, inlen, 1);
      EXPECT_FALSE((inflate_flags & NGHTTP2_HD_INFLATE_EMIT) == 0 && inlen == 0);
      EXPECT_LE(0, result);
      in += result;
      inlen -= result;

      // Verifies decoded HEADERS are as expected.
      if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
        EXPECT_EQ(std::string(reinterpret_cast<char*>(nv.name), nv.namelen), header.first);
        EXPECT_EQ(std::string(reinterpret_cast<char*>(nv.value), nv.valuelen), header.second);
      }

      if (inflate_flags & NGHTTP2_HD_INFLATE_FINAL) {
        nghttp2_hd_inflate_end_headers(inflater);
      }
    }

    nghttp2_hd_inflate_del(inflater);
  }

  nghttp2_session* session_ = nullptr;
  nghttp2_session_callbacks* callbacks_;
  MetadataEncoder encoder_;
  MetadataDecoder decoder_;
  nghttp2_option* option_;

  // Stores data received by peer.
  TestBuffer output_buffer_;

  // Stores extension payload extracted by on_extension_chunk_recv_callback.
  TestBuffer payload_buffer_;

  // Application data passed to nghttp2.
  UserData user_data_;
};

// Tests encoding small METADATAs, which can fit in one HTTP2 frame.
TEST_F(MetadataEncoderTest, EncodeSmallHeaderBlock) {
  initialize();

  MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
      {"header_key2", "header_value2"},
      {"header_key3", "header_value3"},
      {"header_key4", "header_value4"},
  };

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderTest::verifyMetadata, this, &metadata_map,
                                  std::placeholders::_1);
  decoder_.registerMetadataCallback(cb);

  // Creates metadata payload.
  encoder_.createPayload(metadata_map);
  // Submits METADATA to nghttp2.
  int result = nghttp2_submit_extension(session_, METADATA_FRAME_TYPE, END_METADATA_FLAG, STREAM_ID,
                                        &encoder_.payload());
  EXPECT_EQ(0, result);
  // Sends METADATA to nghttp2.
  result = nghttp2_session_send(session_);
  EXPECT_EQ(0, result);

  // Verifies flag and payload are encoded correctly.
  result = nghttp2_session_mem_recv(session_, output_buffer_.buf, output_buffer_.length);
  // The last frame's flag should be set.
  EXPECT_EQ(END_METADATA_FLAG, user_data_.flag);
  ENVOY_LOG_MISC(error, "++++++++++++++encoded payload old: {}",
                 std::string(reinterpret_cast<char*>(payload_buffer_.buf),
                             payload_buffer_.length));
  //verifyPayload(metadata_map, &(payload_buffer_.buf[0]), payload_buffer_.length);

  cleanUp();
}

/*
// Tests encoding large METADATAs, which can cross multiple HTTP2 frames.
TEST_F(MetadataEncoderTest, EncodeLargeHeaderBlock) {
  initialize();

  MetadataMap metadata_map = {
      {"header_key1", std::string(20000, 'a')},
      {"header_key2", std::string(20000, 'b')},
  };

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderTest::verifyMetadata, this, &metadata_map,
                                  std::placeholders::_1);
  decoder_.registerMetadataCallback(cb);


  // Creates metadata payload.
  encoder_.createPayload(metadata_map);

  // Submits METADATA to nghttp2.
  const uint8_t flag =
      (encoder_.payload().length() > encoder_.getMaxMetadataSize()) ? 0 : END_METADATA_FLAG;
  // The payload can't be submitted within one frame. Callback function will
  // keep submitting until all the payload has been submitted.
  int result = nghttp2_submit_extension(session_, METADATA_FRAME_TYPE, flag, STREAM_ID, nullptr);
  EXPECT_EQ(0, result);

  // Sends METADATA to nghttp2.
  result = nghttp2_session_send(session_);
  EXPECT_EQ(0, result);

  // Verifies flag and payload are encoded correctly.
  result = nghttp2_session_mem_recv(session_, output_buffer_.buf, output_buffer_.length);
  // The last frame's flag should be set.
  EXPECT_EQ(END_METADATA_FLAG, user_data_.flag);
  //verifyPayload(metadata_map, &(payload_buffer_.buf[0]), payload_buffer_.length);

  cleanUp();
}

TEST_F(MetadataEncoderTest, TestEmptyMetadataMapFailToEncode) {
  initialize();

  MetadataMap metadata_map;
  EXPECT_FALSE(encoder_.createPayload(metadata_map));

  MetadataMap metadata_map1 = {
      {"header_key1", "header_value1"},
      {"header_key2", "header_value2"},
  };
  MetadataMap metadata_map2 = {
      {"header_key3", "header_value3"},
      {"header_key4", "header_value4"},
  };
  EXPECT_TRUE(encoder_.createPayload(metadata_map1));
  EXPECT_FALSE(encoder_.createPayload(metadata_map2));

  cleanUp();
}

// Verify we don't encode metadata larger than 1M.
TEST_F(MetadataEncoderTest, TestMetadataMapTooBigToEncode) {
  initialize();

  MetadataMap metadata_map = {
      {"header_key1", std::string(1024 * 1024 + 1, 'a')},
  };
  EXPECT_FALSE(encoder_.createPayload(metadata_map));

  cleanUp();
}
*/

} // namespace Http2
} // namespace Http
} // namespace Envoy
