#include "extensions/filters/network/thrift_proxy/metadata.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(HeaderTest, HeaderKeyIsNotTransformed) {
  Header hdr("KEY", "VALUE");
  EXPECT_EQ(hdr.key(), "KEY");
  EXPECT_EQ(hdr.value(), "VALUE");
}

TEST(HeaderTest, HeaderIsCopyable) {
  Header hdr("KEY", "VALUE");
  Header hdrCopy(hdr);
  EXPECT_EQ(hdrCopy.key(), "KEY");
  EXPECT_EQ(hdrCopy.value(), "VALUE");
}

TEST(HeaderMapTest, AddHeaders) {
  HeaderMap headers;
  headers.add(Header("k", "v"));

  Header* hdr = headers.get("k");
  EXPECT_NE(hdr, nullptr);
  EXPECT_EQ(hdr->key(), "k");
  EXPECT_EQ(hdr->value(), "v");
}

TEST(HeaderMapTest, GetHeaders) {
  HeaderMap headers({
      {"a", "b"},
      {"c", "d"},
      {"e", "f"},
  });

  EXPECT_EQ(headers.get("a")->value(), "b");
  EXPECT_EQ(headers.get("c")->value(), "d");
  EXPECT_EQ(headers.get("e")->value(), "f");
}

TEST(HeaderMapTest, Clear) {
  HeaderMap headers({
      {"a", "b"},
      {"c", "d"},
      {"e", "f"},
  });

  headers.clear();
  EXPECT_EQ(headers.get("a"), nullptr);
  EXPECT_EQ(headers.get("c"), nullptr);
  EXPECT_EQ(headers.get("e"), nullptr);
}

TEST(HeaderMapTest, Size) {
  HeaderMap headers({
      {"a", "b"},
      {"c", "d"},
      {"e", "f"},
  });

  EXPECT_EQ(3U, headers.size());
}

TEST(HeaderMapTest, Equality) {
  HeaderMap headers1({{"FIRST", "1"}, {"Second", "2"}});
  HeaderMap headers2({{"FIRST", "1"}, {"Second", "2"}});
  HeaderMap headers3({{"FIRST", "1"}});
  HeaderMap headers4({{"FIRST", "_"}, {"Second", "2"}});
  HeaderMap headers5({{"First", "1"}, {"Second", "2"}});

  EXPECT_EQ(headers1, headers2);
  EXPECT_EQ(headers2, headers1);

  EXPECT_FALSE(headers1 == headers3);
  EXPECT_FALSE(headers3 == headers1);

  EXPECT_FALSE(headers1 == headers4);
  EXPECT_FALSE(headers4 == headers1);

  EXPECT_FALSE(headers1 == headers5);
  EXPECT_FALSE(headers5 == headers1);
}

TEST(HeaderMapTest, Iteration) {
  HeaderMap headers({{"first", "1"}, {"second", "2"}});

  int i = 0;
  for (const Header& header : headers) {
    switch (i) {
    case 0:
      EXPECT_EQ("first", header.key());
      EXPECT_EQ("1", header.value());
      break;
    case 1:
      EXPECT_EQ("second", header.key());
      EXPECT_EQ("2", header.value());
      break;
    default:
      ASSERT(false);
      break;
    }
    i++;
  }
}

TEST(HeaderMapTest, CopyConstructor) {
  HeaderMap headers({{"first", "1"}, {"second", "2"}, {"third", "3"}});
  HeaderMap copy(headers);
  EXPECT_EQ(copy, headers);
}

TEST(MessageMetadataTest, Fields) {
  MessageMetadata metadata;

  EXPECT_FALSE(metadata.hasFrameSize());
  EXPECT_THROW(metadata.frameSize(), absl::bad_optional_access);
  metadata.setFrameSize(100);
  EXPECT_TRUE(metadata.hasFrameSize());
  EXPECT_EQ(100, metadata.frameSize());

  EXPECT_FALSE(metadata.hasProtocol());
  EXPECT_THROW(metadata.protocol(), absl::bad_optional_access);
  metadata.setProtocol(ProtocolType::Binary);
  EXPECT_TRUE(metadata.hasProtocol());
  EXPECT_EQ(ProtocolType::Binary, metadata.protocol());

  EXPECT_FALSE(metadata.hasMethodName());
  EXPECT_THROW(metadata.methodName(), absl::bad_optional_access);
  metadata.setMethodName("method");
  EXPECT_TRUE(metadata.hasMethodName());
  EXPECT_EQ("method", metadata.methodName());

  EXPECT_FALSE(metadata.hasMessageType());
  EXPECT_THROW(metadata.messageType(), absl::bad_optional_access);
  metadata.setMessageType(MessageType::Call);
  EXPECT_TRUE(metadata.hasMessageType());
  EXPECT_EQ(MessageType::Call, metadata.messageType());

  EXPECT_FALSE(metadata.hasSequenceId());
  EXPECT_THROW(metadata.sequenceId(), absl::bad_optional_access);
  metadata.setSequenceId(101);
  EXPECT_TRUE(metadata.hasSequenceId());
  EXPECT_EQ(101, metadata.sequenceId());

  EXPECT_FALSE(metadata.hasAppException());
  EXPECT_THROW(metadata.appExceptionType(), absl::bad_optional_access);
  EXPECT_THROW(metadata.appExceptionMessage(), absl::bad_optional_access);
  metadata.setAppException(AppExceptionType::InternalError, "oops");
  EXPECT_TRUE(metadata.hasAppException());
  EXPECT_EQ(AppExceptionType::InternalError, metadata.appExceptionType());
  EXPECT_EQ("oops", metadata.appExceptionMessage());
}

TEST(MessageMetadataTest, Headers) {
  MessageMetadata metadata;

  EXPECT_TRUE(metadata.headers().empty());

  metadata.addHeader(Header("k", "v"));
  EXPECT_FALSE(metadata.headers().empty());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
