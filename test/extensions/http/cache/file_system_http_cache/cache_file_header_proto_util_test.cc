#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header.pb.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {
namespace {

using testing::AllOf;

constexpr char test_header_proto[] = R"(
  key:
    host: "banana"
  metadata_response_time:
    seconds: 1234
  headers:
  - key: "test_header"
    value: "test_value"
  - key: "second_header"
    value: "second_value"
  - key: "second_header"
    value: "additional_value"
)";

constexpr char test_trailer_proto[] = R"(
  trailers:
  - key: "test_trailer"
    value: "test_value"
  - key: "second_trailer"
    value: "second_value"
  - key: "second_trailer"
    value: "additional_value"
)";

TEST(CacheFileHeaderProtoUtil, MakeCacheFileHeaderProtoFromHeadersAndMetadata) {
  Http::TestResponseHeaderMapImpl headers{
      {"test_header", "test_value"},
      {"second_header", "second_value"},
      {"second_header", "additional_value"},
  };
  ResponseMetadata metadata{Envoy::SystemTime{std::chrono::seconds{1234}}};
  Key key;
  key.set_host("banana");
  CacheFileHeader result = makeCacheFileHeaderProto(key, headers, metadata);
  CacheFileHeader expected;
  TestUtility::loadFromYaml(test_header_proto, expected);
  EXPECT_THAT(result, ProtoEq(expected));
}

TEST(CacheFileHeaderProtoUtil, MakeCacheFileTrailerProto) {
  Http::TestResponseTrailerMapImpl trailers{
      {"test_trailer", "test_value"},
      {"second_trailer", "second_value"},
      {"second_trailer", "additional_value"},
  };
  CacheFileTrailer result = makeCacheFileTrailerProto(trailers);
  CacheFileTrailer expected;
  TestUtility::loadFromYaml(test_trailer_proto, expected);
  EXPECT_THAT(result, ProtoEq(expected));
}

TEST(CacheFileHeaderProtoUtil, HeaderProtoSize) {
  CacheFileHeader header_proto;
  TestUtility::loadFromYaml(test_header_proto, header_proto);
  std::string serialized = header_proto.SerializeAsString();
  EXPECT_EQ(serialized.size(), headerProtoSize(header_proto));
}

TEST(CacheFileHeaderProtoUtil, BufferFromProtoForHeader) {
  CacheFileHeader header_proto;
  TestUtility::loadFromYaml(test_header_proto, header_proto);
  std::string serialized = header_proto.SerializeAsString();
  Buffer::OwnedImpl buffer = bufferFromProto(header_proto);
  EXPECT_EQ(serialized, buffer.toString());
}

TEST(CacheFileHeaderProtoUtil, BufferFromProtoForTrailer) {
  CacheFileTrailer trailer_proto;
  TestUtility::loadFromYaml(test_trailer_proto, trailer_proto);
  std::string serialized = trailer_proto.SerializeAsString();
  Buffer::OwnedImpl buffer = bufferFromProto(trailer_proto);
  EXPECT_EQ(serialized, buffer.toString());
}

TEST(CacheFileHeaderProtoUtil, SerializedStringFromProto) {
  CacheFileHeader header_proto;
  TestUtility::loadFromYaml(test_header_proto, header_proto);
  std::string serialized = header_proto.SerializeAsString();
  std::string serialized_through_helper = serializedStringFromProto(header_proto);
  EXPECT_EQ(serialized, serialized_through_helper);
}

MATCHER_P2(HasKeyValue, key, value, "") {
  Http::HeaderMap::GetResult values = arg->get(Http::LowerCaseString{key});
  for (size_t i = 0; i < values.size(); i++) {
    if (values[i]->value() == value) {
      return true;
    }
  }
  return false;
}

TEST(CacheFileHeaderProtoUtil, HeadersFromHeaderProto) {
  CacheFileHeader header_proto;
  TestUtility::loadFromYaml(test_header_proto, header_proto);
  auto headers = headersFromHeaderProto(header_proto);
  EXPECT_THAT(headers, AllOf(HasKeyValue("test_header", "test_value"),
                             HasKeyValue("second_header", "second_value"),
                             HasKeyValue("second_header", "additional_value")));
}

TEST(CacheFileHeaderProtoUtil, TrailersFromTrailerProto) {
  CacheFileTrailer trailer_proto;
  TestUtility::loadFromYaml(test_trailer_proto, trailer_proto);
  auto trailers = trailersFromTrailerProto(trailer_proto);
  EXPECT_THAT(trailers, AllOf(HasKeyValue("test_trailer", "test_value"),
                              HasKeyValue("second_trailer", "second_value"),
                              HasKeyValue("second_trailer", "additional_value")));
}

TEST(CacheFileHeaderProtoUtil, MetadataFromHeaderProto) {
  CacheFileHeader header_proto;
  TestUtility::loadFromYaml(test_header_proto, header_proto);
  auto metadata = metadataFromHeaderProto(header_proto);
  EXPECT_EQ(metadata.response_time_, Envoy::SystemTime{std::chrono::seconds{1234}});
}

TEST(CacheFileHeaderProtoUtil, MakeCacheFileHeaderProtoFromBuffer) {
  CacheFileHeader header_proto;
  TestUtility::loadFromYaml(test_header_proto, header_proto);
  Buffer::OwnedImpl buffer = bufferFromProto(header_proto);
  CacheFileHeader header_proto_from_buffer = makeCacheFileHeaderProto(buffer);
  EXPECT_THAT(header_proto, ProtoEq(header_proto_from_buffer));
}

TEST(CacheFileHeaderProtoUtil, UpdateProtoFromHeadersAndMetadata) {
  CacheFileHeader header_proto;
  TestUtility::loadFromYaml(test_header_proto, header_proto);
  CacheFileHeader new_header_proto;
  TestUtility::loadFromYaml(R"(
    key:
      host: "banana"
    metadata_response_time:
      seconds: 12345
    headers:
    - key: "second_header"
      value: "new_second_value"
    - key: "second_header"
      value: "additional_second_value"
    - key: "third_header"
      value: "third_value"
    - key: "etag"
      value: "should_be_ignored"
    - key: "content-length"
      value: "should_be_ignored"
    - key: "content-range"
      value: "should_be_ignored"
    - key: "vary"
      value: "should_be_ignored"
  )",
                            new_header_proto);
  updateProtoFromHeadersAndMetadata(header_proto, new_header_proto);
  CacheFileHeader expected_header_proto;
  TestUtility::loadFromYaml(R"(
    key:
      host: "banana"
    # metadata should have been updated.
    metadata_response_time:
      seconds: 12345
    headers:
    # test_header should be retained from the original headers.
    - key: "test_header"
      value: "test_value"
    # second_header should be overwritten.
    - key: "second_header"
      value: "new_second_value"
    # added value on the same key should appear.
    - key: "second_header"
      value: "additional_second_value"
    # added value with a new key should appear.
    - key: "third_header"
      value: "third_value"
    # ignored keys should have been discarded.
  )",
                            expected_header_proto);
  EXPECT_THAT(header_proto, ProtoEq(expected_header_proto));
}

} // namespace
} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
