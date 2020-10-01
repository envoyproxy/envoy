#include <string>

#include "envoy/http/metadata_interface.h"

#include "common/http/http2/metadata_encoder.h"

#include "test/common/http/http2/http2_frame.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Http2 {
// From metadata map
TEST(EqualityMetadataFrame, Http2FrameTest) {
  MetadataMap metadataMap = {{"Connections", "15"}, {"Timeout Seconds", "10"}};
  Http2Frame http2FrameFromUtility = Http2Frame::makeMetadataFrameFromMetadataMap(
      1, metadataMap, Http2Frame::MetadataFlags::EndMetadata);
  std::string payloadFromHttp2Frame(http2FrameFromUtility);
  // Note: the actual encoding of the metadata map is non-deterministic and flaky. This is okay!
  ASSERT_EQ(static_cast<int>(http2FrameFromUtility.type()), 0x4D); // type
  ASSERT_EQ(payloadFromHttp2Frame[4], 4);                          // flags
  ASSERT_EQ(std::to_string(payloadFromHttp2Frame[8]),
            std::to_string(1)); // stream_id
}
} // namespace Http2
} // namespace Http
} // namespace Envoy
