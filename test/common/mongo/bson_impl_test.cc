#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/mongo/bson_impl.h"

#include "test/test_common/printers.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Bson {

TEST(BsonImplTest, BadCast) {
  DocumentSharedPtr doc = DocumentImpl::create()->addString("hello", "world");
  EXPECT_THROW(doc->values().front()->asDouble(), EnvoyException);
}

TEST(BsonImplTest, Equal) {
  DocumentSharedPtr doc1 = DocumentImpl::create();
  DocumentSharedPtr doc2 = DocumentImpl::create()->addString("hello", "world");
  EXPECT_FALSE(*doc1 == *doc2);

  doc1->addDouble("hello", 2.0);
  EXPECT_FALSE(*doc1 == *doc2);
}

TEST(BsonImplTest, InvalidMessageLength) {
  Buffer::OwnedImpl buffer;
  BufferHelper::writeInt32(buffer, 100);
  EXPECT_THROW(DocumentImpl::create(buffer), EnvoyException);
}

TEST(BsonImplTest, InvalidElementType) {
  Buffer::OwnedImpl buffer;
  std::string key_name("hello");
  BufferHelper::writeInt32(buffer, 4 + 1 + key_name.size() + 1);
  uint8_t invalid_element_type = 0x20;
  buffer.add(&invalid_element_type, sizeof(invalid_element_type));
  BufferHelper::writeCString(buffer, key_name);
  EXPECT_THROW(DocumentImpl::create(buffer), EnvoyException);
}

TEST(BsonImplTest, InvalodDocumentTermination) {
  Buffer::OwnedImpl buffer;
  BufferHelper::writeInt32(buffer, 5);
  uint8_t invalid_document_end = 0x1;
  buffer.add(&invalid_document_end, sizeof(invalid_document_end));
  EXPECT_THROW(DocumentImpl::create(buffer), EnvoyException);
}

TEST(BufferHelperTest, InvalidSize) {
  Buffer::OwnedImpl buffer;
  EXPECT_THROW(BufferHelper::peakInt32(buffer), EnvoyException);
  EXPECT_THROW(BufferHelper::removeByte(buffer), EnvoyException);
  EXPECT_THROW(BufferHelper::removeBytes(buffer, nullptr, 1), EnvoyException);
  EXPECT_THROW(BufferHelper::removeCString(buffer), EnvoyException);
  EXPECT_THROW(BufferHelper::removeDouble(buffer), EnvoyException);
  EXPECT_THROW(BufferHelper::removeInt64(buffer), EnvoyException);
  EXPECT_THROW(BufferHelper::removeString(buffer), EnvoyException);
}

} // namespace Bson
} // namespace Envoy
