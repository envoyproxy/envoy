#pragma once

#include <array>
#include <list>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {
namespace Bson {

/**
 * Implementation of http://bsonspec.org/spec.html
 */
class Document;
typedef std::shared_ptr<Document> DocumentSharedPtr;

/**
 * A BSON document field. This is essentially a variably typed parameter that can be "cast" to
 * the correct type via the as*() functions.
 */
class Field {
public:
  /**
   * Raw field type.
   */
  enum class Type : uint8_t {
    DOUBLE = 0x01,
    STRING = 0x02,
    DOCUMENT = 0x03,
    ARRAY = 0x04,
    BINARY = 0x05,
    OBJECT_ID = 0x07,
    BOOLEAN = 0x08,
    DATETIME = 0x09,
    NULL_VALUE = 0x0A,
    REGEX = 0x0B,
    INT32 = 0x10,
    TIMESTAMP = 0x11,
    INT64 = 0x12
  };

  /**
   * 12 byte ObjectId type.
   */
  typedef std::array<uint8_t, 12> ObjectId;

  /**
   * Regex type.
   */
  struct Regex {
    bool operator==(const Regex& rhs) const {
      return pattern_ == rhs.pattern_ && options_ == rhs.options_;
    }

    std::string pattern_;
    std::string options_;
  };

  virtual ~Field() {}

  virtual double asDouble() const PURE;
  virtual const std::string& asString() const PURE;
  virtual const Document& asDocument() const PURE;
  virtual const Document& asArray() const PURE;
  virtual const std::string& asBinary() const PURE;
  virtual const ObjectId& asObjectId() const PURE;
  virtual bool asBoolean() const PURE;
  virtual int64_t asDatetime() const PURE;
  virtual const Regex& asRegex() const PURE;
  virtual int32_t asInt32() const PURE;
  virtual int64_t asTimestamp() const PURE;
  virtual int64_t asInt64() const PURE;

  virtual int32_t byteSize() const PURE;
  virtual void encode(Buffer::Instance& output) const PURE;
  virtual const std::string& key() const PURE;
  virtual bool operator==(const Field& rhs) const PURE;
  virtual std::string toString() const PURE;
  virtual Type type() const PURE;
};

typedef std::unique_ptr<Field> FieldPtr;

/**
 * A BSON document. add*() is used to add strongly typed fields.
 */
class Document {
public:
  virtual ~Document() {}

  virtual DocumentSharedPtr addDouble(const std::string& key, double value) PURE;
  virtual DocumentSharedPtr addString(const std::string& key, std::string&& value) PURE;
  virtual DocumentSharedPtr addDocument(const std::string& key, DocumentSharedPtr value) PURE;
  virtual DocumentSharedPtr addArray(const std::string& key, DocumentSharedPtr value) PURE;
  virtual DocumentSharedPtr addBinary(const std::string& key, std::string&& value) PURE;
  virtual DocumentSharedPtr addObjectId(const std::string& key, Field::ObjectId&& value) PURE;
  virtual DocumentSharedPtr addBoolean(const std::string& key, bool value) PURE;
  virtual DocumentSharedPtr addDatetime(const std::string& key, int64_t value) PURE;
  virtual DocumentSharedPtr addNull(const std::string& key) PURE;
  virtual DocumentSharedPtr addRegex(const std::string& key, Field::Regex&& value) PURE;
  virtual DocumentSharedPtr addInt32(const std::string& key, int32_t value) PURE;
  virtual DocumentSharedPtr addTimestamp(const std::string& key, int64_t value) PURE;
  virtual DocumentSharedPtr addInt64(const std::string& key, int64_t value) PURE;

  virtual bool operator==(const Document& rhs) const PURE;
  virtual int32_t byteSize() const PURE;
  virtual void encode(Buffer::Instance& output) const PURE;
  virtual const Field* find(const std::string& name) const PURE;
  virtual const Field* find(const std::string& name, Field::Type type) const PURE;
  virtual std::string toString() const PURE;
  virtual const std::list<FieldPtr>& values() const PURE;
};

} // namespace Bson
} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
