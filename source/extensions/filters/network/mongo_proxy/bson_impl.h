#pragma once

#include <list>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/common/logger.h"

#include "extensions/filters/network/mongo_proxy/bson.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {
namespace Bson {

/**
 * IO helpers for reading/writing BSON data from/to a buffer.
 */
class BufferHelper {
public:
  static int32_t peakInt32(Buffer::Instance& data);
  static uint8_t removeByte(Buffer::Instance& data);
  static void removeBytes(Buffer::Instance& data, uint8_t* out, size_t out_len);
  static std::string removeCString(Buffer::Instance& data);
  static double removeDouble(Buffer::Instance& data);
  static int32_t removeInt32(Buffer::Instance& data);
  static int64_t removeInt64(Buffer::Instance& data);
  static std::string removeString(Buffer::Instance& data);
  static std::string removeBinary(Buffer::Instance& data);
  static void writeCString(Buffer::Instance& data, const std::string& value);
  static void writeInt32(Buffer::Instance& data, int32_t value);
  static void writeInt64(Buffer::Instance& data, int64_t value);
  static void writeDouble(Buffer::Instance& data, double value);
  static void writeString(Buffer::Instance& data, const std::string& value);
  static void writeBinary(Buffer::Instance& data, const std::string& value);
};

class FieldImpl : public Field {
public:
  explicit FieldImpl(const std::string& key, double value) : type_(Type::DOUBLE), key_(key) {
    value_.double_value_ = value;
  }

  explicit FieldImpl(Type type, const std::string& key, std::string&& value)
      : type_(type), key_(key) {
    value_.string_value_ = std::move(value);
  }

  explicit FieldImpl(Type type, const std::string& key, DocumentSharedPtr value)
      : type_(type), key_(key) {
    value_.document_value_ = value;
  }

  explicit FieldImpl(const std::string& key, ObjectId&& value) : type_(Type::OBJECT_ID), key_(key) {
    value_.object_id_value_ = std::move(value);
  }

  explicit FieldImpl(const std::string& key, bool value) : type_(Type::BOOLEAN), key_(key) {
    value_.bool_value_ = value;
  }

  explicit FieldImpl(Type type, const std::string& key, int64_t value) : type_(type), key_(key) {
    value_.int64_value_ = value;
  }

  explicit FieldImpl(const std::string& key) : type_(Type::NULL_VALUE), key_(key) {}

  explicit FieldImpl(const std::string& key, Regex&& value) : type_(Type::REGEX), key_(key) {
    value_.regex_value_ = std::move(value);
  }

  explicit FieldImpl(const std::string& key, int32_t value) : type_(Type::INT32), key_(key) {
    value_.int32_value_ = value;
  }

  // Bson::Field
  double asDouble() const override {
    checkType(Type::DOUBLE);
    return value_.double_value_;
  }

  const std::string& asString() const override {
    checkType(Type::STRING);
    return value_.string_value_;
  }

  const Document& asDocument() const override {
    checkType(Type::DOCUMENT);
    return *value_.document_value_;
  }

  const Document& asArray() const override {
    checkType(Type::ARRAY);
    return *value_.document_value_;
  }

  const std::string& asBinary() const override {
    checkType(Type::BINARY);
    return value_.string_value_;
  }

  const ObjectId& asObjectId() const override {
    checkType(Type::OBJECT_ID);
    return value_.object_id_value_;
  }

  bool asBoolean() const override {
    checkType(Type::BOOLEAN);
    return value_.bool_value_;
  }

  int64_t asDatetime() const override {
    checkType(Type::DATETIME);
    return value_.int64_value_;
  }

  const Regex& asRegex() const override {
    checkType(Type::REGEX);
    return value_.regex_value_;
  }

  int32_t asInt32() const override {
    checkType(Type::INT32);
    return value_.int32_value_;
  }

  int64_t asTimestamp() const override {
    checkType(Type::TIMESTAMP);
    return value_.int64_value_;
  }

  int64_t asInt64() const override {
    checkType(Type::INT64);
    return value_.int64_value_;
  }

  int32_t byteSize() const override;
  void encode(Buffer::Instance& output) const override;
  const std::string& key() const override { return key_; }
  bool operator==(const Field& rhs) const override;
  std::string toString() const override;
  Type type() const override { return type_; }

private:
  void checkType(Type type) const {
    if (type_ != type) {
      throw EnvoyException("invalid BSON field type cast");
    }
  }

  /**
   * All of the possible variadic values that a field can be.
   * TODO(mattklein123): Make this a C++11 union to save a little space and time.
   */
  struct Value {
    double double_value_;
    std::string string_value_;
    DocumentSharedPtr document_value_;
    Field::ObjectId object_id_value_;
    bool bool_value_;
    int32_t int32_value_;
    int64_t int64_value_;
    Regex regex_value_;
  };

  Field::Type type_;
  std::string key_;
  Value value_;
};

class DocumentImpl : public Document,
                     Logger::Loggable<Logger::Id::mongo>,
                     public std::enable_shared_from_this<DocumentImpl> {
public:
  static DocumentSharedPtr create() { return DocumentSharedPtr{new DocumentImpl()}; }
  static DocumentSharedPtr create(Buffer::Instance& data) {
    std::shared_ptr<DocumentImpl> new_doc{new DocumentImpl()};
    new_doc->fromBuffer(data);
    return new_doc;
  }

  // Mongo::Document
  DocumentSharedPtr addDouble(const std::string& key, double value) override {
    fields_.emplace_back(new FieldImpl(key, value));
    return shared_from_this();
  }

  DocumentSharedPtr addString(const std::string& key, std::string&& value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::STRING, key, std::move(value)));
    return shared_from_this();
  }

  DocumentSharedPtr addDocument(const std::string& key, DocumentSharedPtr value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::DOCUMENT, key, value));
    return shared_from_this();
  }

  DocumentSharedPtr addArray(const std::string& key, DocumentSharedPtr value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::ARRAY, key, value));
    return shared_from_this();
  }

  DocumentSharedPtr addBinary(const std::string& key, std::string&& value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::BINARY, key, std::move(value)));
    return shared_from_this();
  }

  DocumentSharedPtr addObjectId(const std::string& key, Field::ObjectId&& value) override {
    fields_.emplace_back(new FieldImpl(key, std::move(value)));
    return shared_from_this();
  }

  DocumentSharedPtr addBoolean(const std::string& key, bool value) override {
    fields_.emplace_back(new FieldImpl(key, value));
    return shared_from_this();
  }

  DocumentSharedPtr addDatetime(const std::string& key, int64_t value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::DATETIME, key, value));
    return shared_from_this();
  }

  DocumentSharedPtr addNull(const std::string& key) override {
    fields_.emplace_back(new FieldImpl(key));
    return shared_from_this();
  }

  DocumentSharedPtr addRegex(const std::string& key, Field::Regex&& value) override {
    fields_.emplace_back(new FieldImpl(key, std::move(value)));
    return shared_from_this();
  }

  DocumentSharedPtr addInt32(const std::string& key, int32_t value) override {
    fields_.emplace_back(new FieldImpl(key, value));
    return shared_from_this();
  }

  DocumentSharedPtr addTimestamp(const std::string& key, int64_t value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::TIMESTAMP, key, value));
    return shared_from_this();
  }

  DocumentSharedPtr addInt64(const std::string& key, int64_t value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::INT64, key, value));
    return shared_from_this();
  }

  bool operator==(const Document& rhs) const override;
  int32_t byteSize() const override;
  void encode(Buffer::Instance& output) const override;
  const Field* find(const std::string& name) const override;
  const Field* find(const std::string& name, Field::Type type) const override;
  std::string toString() const override;
  const std::list<FieldPtr>& values() const override { return fields_; }

private:
  DocumentImpl() {}

  void fromBuffer(Buffer::Instance& data);

  std::list<FieldPtr> fields_;
};

} // namespace Bson
} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
