#include <string>

#include "envoy/http/metadata_interface.h"

#include "source/common/http/http2/metadata_encoder.h"

#include "test/common/http/http2/http2_frame.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Http2 {

// Test metadata frame creation and properties
TEST(EqualityMetadataFrame, Http2FrameTest) {
  MetadataMap metadataMap = {{"Connections", "15"}, {"Timeout Seconds", "10"}};
  Http2Frame http2FrameFromUtility = Http2Frame::makeMetadataFrameFromMetadataMap(
      1, metadataMap, Http2Frame::MetadataFlags::EndMetadata);
  std::string payloadFromHttp2Frame(http2FrameFromUtility);
  // Note: the actual encoding of the metadata map is non-deterministic and flaky. This is okay!
  ASSERT_EQ(static_cast<int>(http2FrameFromUtility.type()), 0x4D);        // type
  ASSERT_EQ(payloadFromHttp2Frame[4], 4);                                 // flags
  ASSERT_EQ(std::to_string(payloadFromHttp2Frame[8]), std::to_string(1)); // stream_id
}

// Test data frame creation and properties
TEST(DataFrame, Http2FrameTest) {
  std::string testData = "Hello, World!";
  Http2Frame frame = Http2Frame::makeDataFrame(1, testData, Http2Frame::DataFlags::EndStream);

  ASSERT_EQ(frame.type(), Http2Frame::Type::Data);
  ASSERT_TRUE(frame.endStream());
  ASSERT_EQ(frame.streamId(), 1);
  ASSERT_EQ(frame.payloadSize(), testData.size());
}

// Test headers frame creation and properties
TEST(HeadersFrame, Http2FrameTest) {
  Http2Frame frame = Http2Frame::makeHeadersFrameWithStatus(
      "200", 1,
      static_cast<Http2Frame::HeadersFlags>(
          orFlags(Http2Frame::HeadersFlags::EndStream, Http2Frame::HeadersFlags::EndHeaders)));

  ASSERT_EQ(frame.type(), Http2Frame::Type::Headers);
  ASSERT_TRUE(frame.endStream());
  ASSERT_EQ(frame.streamId(), 1);
}

// Test settings frame creation and properties
TEST(SettingsFrame, Http2FrameTest) {
  std::list<std::pair<uint16_t, uint32_t>> settings = {
      {0x1, 0x1000}, // SETTINGS_HEADER_TABLE_SIZE
      {0x2, 0x1}     // SETTINGS_ENABLE_PUSH
  };
  Http2Frame frame = Http2Frame::makeSettingsFrame(Http2Frame::SettingsFlags::Ack, settings);

  ASSERT_EQ(frame.type(), Http2Frame::Type::Settings);
  ASSERT_EQ(frame.payloadSize(), settings.size() * 6); // Each setting is 6 bytes
}

// Test window update frame creation and properties
TEST(WindowUpdateFrame, Http2FrameTest) {
  Http2Frame frame = Http2Frame::makeWindowUpdateFrame(1, 1024);

  ASSERT_EQ(frame.type(), Http2Frame::Type::WindowUpdate);
  ASSERT_EQ(frame.streamId(), 1);
  ASSERT_EQ(frame.payloadSize(), 4); // Window update payload is always 4 bytes
}

// Test reset stream frame creation and properties
TEST(ResetStreamFrame, Http2FrameTest) {
  Http2Frame frame = Http2Frame::makeResetStreamFrame(1, Http2Frame::ErrorCode::Cancel);

  ASSERT_EQ(frame.type(), Http2Frame::Type::RstStream);
  ASSERT_EQ(frame.streamId(), 1);
  ASSERT_EQ(frame.payloadSize(), 4); // Reset stream payload is always 4 bytes
}

// Test ping frame creation and properties
TEST(PingFrame, Http2FrameTest) {
  std::string pingData = "12345678";
  Http2Frame frame = Http2Frame::makePingFrame(pingData);

  ASSERT_EQ(frame.type(), Http2Frame::Type::Ping);
  ASSERT_EQ(frame.payloadSize(), 8); // Ping payload is always 8 bytes
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
