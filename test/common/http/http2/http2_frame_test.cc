#include "common/http/http2/metadata_encoder.h"
#include "envoy/http/metadata_interface.h"
#include "test/common/http/http2/http2_frame.h"


#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Http2 {

    class EqualityMetadataFrame : public ::testing::Test {};

    //From metadata map 
    TEST(EqualityMetadataFrame, Http2FrameTest) {
        MetadataMap metadataMap = {{"Connections", "15"}, {"Timeout Seconds", "10"}};
        Http2Frame http2FrameFromUtility = Http2Frame::makeMetadataFrameFromMetadataMap(1, metadataMap, Http2Frame::MetadataFlags::EndMetadata);
        MetadataEncoder metadataEncoder{};
        Http::MetadataMapPtr metadataMapPtr = std::make_unique<Http::MetadataMap>(metadataMap);
        Http::MetadataMapVector metadata_map_vector;
        metadata_map_vector.push_back(std::move(metadataMapPtr));
        metadataEncoder.createPayload(metadata_map_vector);
        std::string payloadFromEncoder = metadataEncoder.payload();
        std::string payloadFromHttp2Frame(http2FrameFromUtility);
        ASSERT_EQ(payloadFromEncoder, payloadFromHttp2Frame.substr(9, payloadFromHttp2Frame.size() - 9)); //9 octets of headers
    }
} // namespace Http2
} // namespace Http
} // namespace Envoy