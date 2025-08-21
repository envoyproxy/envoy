#pragma once

#include <list>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/extensions/filters/network/mongo_proxy/bson.h"

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
  static int32_t peekInt32(Buffer::Instance& data);
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
  explicit FieldImpl(const std::string& key, double value) : type_(Type::Double), key_(key) {
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

  explicit FieldImpl(const std::string& key, ObjectId&& value) : type_(Type::ObjectId), key_(key) {
    value_.object_id_value_ = std::move(value);
  }

  explicit FieldImpl(const std::string& key, bool value) : type_(Type::Boolean), key_(key) {
    value_.bool_value_ = value;
  }

  explicit FieldImpl(Type type, const std::string& key, int64_t value) : type_(type), key_(key) {
    value_.int64_value_ = value;
  }

  explicit FieldImpl(const std::string& key) : type_(Type::NullValue), key_(key) {}

  explicit FieldImpl(const std::string& key, Regex&& value) : type_(Type::Regex), key_(key) {
    value_.regex_value_ = std::move(value);
  }

  explicit FieldImpl(const std::string& key, int32_t value) : type_(Type::Int32), key_(key) {
    value_.int32_value_ = value;
  }

  // Bson::Field
  double asDouble() const override {
    checkType(Type::Double);
    return value_.double_value_;
  }

  const std::string& asString() const override {
    checkType(Type::String);
    return value_.string_value_;
  }

  const std::string& asSymbol() const override {
    checkType(Type::Symbol);
    return value_.string_value_;
  }

  const Document& asDocument() const override {
    checkType(Type::Document);
    return *value_.document_value_;
  }

  const Document& asArray() const override {
    checkType(Type::Array);
    return *value_.document_value_;
  }

  const std::string& asBinary() const override {
    checkType(Type::Binary);
    return value_.string_value_;
  }

  const ObjectId& asObjectId() const override {
    checkType(Type::ObjectId);
    return value_.object_id_value_;
  }

  bool asBoolean() const override {
    checkType(Type::Boolean);
    return value_.bool_value_;
  }

  int64_t asDatetime() const override {
    checkType(Type::Datetime);
    return value_.int64_value_;
  }

  const Regex& asRegex() const override {
    checkType(Type::Regex);
    return value_.regex_value_;
  }

  int32_t asInt32() const override {
    checkType(Type::Int32);
    return value_.int32_value_;
  }

  int64_t asTimestamp() const override {
    checkType(Type::Timestamp);
    return value_.int64_value_;
  }

  int64_t asInt64() const override {
    checkType(Type::Int64);
    return value_.int64_value_;
  }

  int32_t byteSize() const override;
  void encode(Buffer::Instance& output) const override;
  const std::string& key() const override { return key_; }
  bool operator==(const Field& rhs) const override;
  std::string toString() const override;
  Type type() const override { return type_; }

private:
  void checkType(Type type) const;

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
    fields_.emplace_back(new FieldImpl(Field::Type::String, key, std::move(value)));
    return shared_from_this();
  }

  DocumentSharedPtr addSymbol(const std::string& key, std::string&& value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::Symbol, key, std::move(value)));
    return shared_from_this();
  }

  DocumentSharedPtr addDocument(const std::string& key, DocumentSharedPtr value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::Document, key, value));
    return shared_from_this();
  }

  DocumentSharedPtr addArray(const std::string& key, DocumentSharedPtr value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::Array, key, value));
    return shared_from_this();
  }

  DocumentSharedPtr addBinary(const std::string& key, std::string&& value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::Binary, key, std::move(value)));
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
    fields_.emplace_back(new FieldImpl(Field::Type::Datetime, key, value));
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
    fields_.emplace_back(new FieldImpl(Field::Type::Timestamp, key, value));
    return shared_from_this();
  }

  DocumentSharedPtr addInt64(const std::string& key, int64_t value) override {
    fields_.emplace_back(new FieldImpl(Field::Type::Int64, key, value));
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
  DocumentImpl() = default;

  void fromBuffer(Buffer::Instance& data);

  std::list<FieldPtr> fields_;
};

} // namespace Bson
} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
