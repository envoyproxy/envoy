#include <string>

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mongo_proxy/bson_impl.h"

#include "test/test_common/printers.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {
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

TEST(BsonImplTest, SequenceEqual) {
  {
    auto seq1 = SequenceImpl::create();
    auto seq2 = SequenceImpl::create();
    seq1->size(0);
    seq2->size(1);
    EXPECT_FALSE(*seq1 == *seq2);
  }

  {
    auto seq1 = SequenceImpl::create();
    auto seq2 = SequenceImpl::create();
    seq1->size(0);
    seq2->size(0);
    seq1->identifier("id");
    seq2->identifier("id2");
    EXPECT_FALSE(*seq1 == *seq2);
  }

  {
    auto seq1 = SequenceImpl::create();
    auto seq2 = SequenceImpl::create();
    seq1->size(0);
    seq2->size(0);
    seq1->identifier("id");
    seq1->identifier("id");
    seq1->documents().push_back(DocumentImpl::create());
    seq2->documents().push_back(DocumentImpl::create()->addString("hello", "world"));
    EXPECT_FALSE(*seq1 == *seq2);
  }
}

TEST(BsonImplTest, SectionEqual) {
  {
    auto sec1 = SectionImpl::create();
    auto sec2 = SectionImpl::create();
    sec1->payloadType(Section::PayloadType::Document);
    sec2->payloadType(Section::PayloadType::Sequence);
    EXPECT_FALSE(*sec1 == *sec2);
  }

  {
    auto sec1 = SectionImpl::create();
    auto sec2 = SectionImpl::create();
    DocumentSharedPtr doc1 = DocumentImpl::create();
    DocumentSharedPtr doc2 = DocumentImpl::create()->addString("hello", "world");
    sec1->payloadType(Section::PayloadType::Document);
    sec1->payload(std::make_shared<Payload>(Payload(doc1)));
    sec2->payloadType(Section::PayloadType::Document);
    sec2->payload(std::make_shared<Payload>(Payload(doc2)));
    EXPECT_FALSE(*sec1 == *sec2);
  }

  {
    auto sec1 = SectionImpl::create();
    auto sec2 = SectionImpl::create();
    auto seq1 = SequenceImpl::create();
    auto seq2 = SequenceImpl::create();
    seq1->size(0);
    seq2->size(1);
    sec1->payloadType(Section::PayloadType::Sequence);
    sec1->payload(std::make_shared<Payload>(Payload(seq1)));
    sec2->payloadType(Section::PayloadType::Sequence);
    sec2->payload(std::make_shared<Payload>(Payload(seq2)));
    EXPECT_FALSE(*sec1 == *sec2);
  }
}

TEST(BsonImplTest, InvalidSectionPayloadType) {
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeByte(buffer, 3);
    EXPECT_THROW(Bson::SectionImpl::create(buffer), EnvoyException);
  }

  {
    auto section = Bson::SectionImpl::create();
    section->payloadType(3);
    EXPECT_THROW(section->byteSize(), EnvoyException);
  }

  {
    Buffer::OwnedImpl buffer;
    auto section = Bson::SectionImpl::create();
    section->payloadType(3);
    EXPECT_THROW(section->encode(buffer), EnvoyException);
  }

  {
    auto s1 = Bson::SectionImpl::create();
    s1->payloadType(3);
    auto s2 = Bson::SectionImpl::create();
    s2->payloadType(3);
    EXPECT_THROW(*s1 == *s2, EnvoyException);
  }

  {
    auto section = Bson::SectionImpl::create();
    section->payloadType(3);
    EXPECT_THROW(section->toString(), EnvoyException);
  }
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
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW(BufferHelper::peekInt32(buffer), EnvoyException);
    EXPECT_THROW(BufferHelper::removeByte(buffer), EnvoyException);
    EXPECT_THROW(BufferHelper::removeBytes(buffer, nullptr, 1), EnvoyException);
    EXPECT_THROW(BufferHelper::removeCString(buffer), EnvoyException);
    EXPECT_THROW(BufferHelper::removeDouble(buffer), EnvoyException);
    EXPECT_THROW(BufferHelper::removeInt64(buffer), EnvoyException);
    EXPECT_THROW(BufferHelper::removeString(buffer), EnvoyException);
  }

  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeInt32(buffer, 4);
    EXPECT_THROW(BufferHelper::removeString(buffer), EnvoyException);
  }

  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeInt32(buffer, 4);
    uint8_t dummy = 0;
    buffer.add(&dummy, sizeof(dummy));
    EXPECT_THROW(BufferHelper::removeBinary(buffer), EnvoyException);
  }
}

} // namespace Bson
} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
