#include "source/common/json/json_internal_legacy.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hash.h"
#include "source/common/common/utility.h"
#include "source/common/protobuf/utility.h"

// Do not let RapidJson leak outside of this file.
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/reader.h"
#include "rapidjson/schema.h"
#include "rapidjson/stream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Json {
namespace RapidJson {

namespace {
/**
 * Internal representation of Object.
 */
class Field;
using FieldSharedPtr = std::shared_ptr<Field>;

class Field : public Object {
public:
  void setLineNumberStart(uint64_t line_number) { line_number_start_ = line_number; }
  void setLineNumberEnd(uint64_t line_number) { line_number_end_ = line_number; }

  // Container factories for handler.
  static FieldSharedPtr createObject() { return FieldSharedPtr{new Field(Type::Object)}; }
  static FieldSharedPtr createArray() { return FieldSharedPtr{new Field(Type::Array)}; }
  static FieldSharedPtr createNull() { return FieldSharedPtr{new Field(Type::Null)}; }

  bool isArray() const override { return type_ == Type::Array; }
  bool isObject() const override { return type_ == Type::Object; }

  // Value factory.
  template <typename T> static FieldSharedPtr createValue(T value) {
    return FieldSharedPtr{new Field(value)}; // NOLINT(modernize-make-shared)
  }

  void append(FieldSharedPtr field_ptr) {
    checkType(Type::Array);
    value_.array_value_.push_back(field_ptr);
  }
  void insert(const std::string& key, FieldSharedPtr field_ptr) {
    checkType(Type::Object);
    value_.object_value_[key] = field_ptr;
  }

  uint64_t hash() const override;

  bool getBoolean(const std::string& name) const override;
  bool getBoolean(const std::string& name, bool default_value) const override;
  double getDouble(const std::string& name) const override;
  double getDouble(const std::string& name, double default_value) const override;
  int64_t getInteger(const std::string& name) const override;
  int64_t getInteger(const std::string& name, int64_t default_value) const override;
  ObjectSharedPtr getObject(const std::string& name, bool allow_empty) const override;
  std::vector<ObjectSharedPtr> getObjectArray(const std::string& name,
                                              bool allow_empty) const override;
  std::string getString(const std::string& name) const override;
  std::string getString(const std::string& name, const std::string& default_value) const override;
  std::vector<std::string> getStringArray(const std::string& name, bool allow_empty) const override;
  std::vector<ObjectSharedPtr> asObjectArray() const override;
  std::string asString() const override { return stringValue(); }
  std::string asJsonString() const override;

  bool empty() const override;
  bool hasObject(const std::string& name) const override;
  void iterate(const ObjectCallback& callback) const override;
  void validateSchema(const std::string& schema) const override;

private:
  enum class Type {
    Array,
    Boolean,
    Double,
    Integer,
    Null,
    Object,
    String,
  };
  static const char* typeAsString(Type t) {
    switch (t) {
    case Type::Array:
      return "Array";
    case Type::Boolean:
      return "Boolean";
    case Type::Double:
      return "Double";
    case Type::Integer:
      return "Integer";
    case Type::Null:
      return "Null";
    case Type::Object:
      return "Object";
    case Type::String:
      return "String";
    }

    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  struct Value {
    std::vector<FieldSharedPtr> array_value_;
    bool boolean_value_;
    double double_value_;
    int64_t integer_value_;
    std::map<std::string, FieldSharedPtr> object_value_;
    std::string string_value_;
  };

  explicit Field(Type type) : type_(type) {}
  explicit Field(const std::string& value) : type_(Type::String) { value_.string_value_ = value; }
  explicit Field(int64_t value) : type_(Type::Integer) { value_.integer_value_ = value; }
  explicit Field(double value) : type_(Type::Double) { value_.double_value_ = value; }
  explicit Field(bool value) : type_(Type::Boolean) { value_.boolean_value_ = value; }

  bool isType(Type type) const { return type == type_; }
  void checkType(Type type) const {
    if (!isType(type)) {
      throw Exception(fmt::format(
          "JSON field from line {} accessed with type '{}' does not match actual type '{}'.",
          line_number_start_, typeAsString(type), typeAsString(type_)));
    }
  }

  // Value return type functions.
  std::string stringValue() const {
    checkType(Type::String);
    return value_.string_value_;
  }
  std::vector<FieldSharedPtr> arrayValue() const {
    checkType(Type::Array);
    return value_.array_value_;
  }
  bool booleanValue() const {
    checkType(Type::Boolean);
    return value_.boolean_value_;
  }
  double doubleValue() const {
    checkType(Type::Double);
    return value_.double_value_;
  }
  int64_t integerValue() const {
    checkType(Type::Integer);
    return value_.integer_value_;
  }

  rapidjson::Document asRapidJsonDocument() const;
  static void buildRapidJsonDocument(const Field& field, rapidjson::Value& value,
                                     rapidjson::Document::AllocatorType& allocator);

  uint64_t line_number_start_ = 0;
  uint64_t line_number_end_ = 0;
  const Type type_;
  Value value_;
};

/**
 * Custom stream to allow access to the line number for each object.
 */
class LineCountingStringStream : public rapidjson::StringStream {
  // Ch is typedef in parent class to handle character encoding.
public:
  LineCountingStringStream(const Ch* src) : rapidjson::StringStream(src), line_number_(1) {}
  Ch Take() {
    Ch ret = rapidjson::StringStream::Take();
    if (ret == '\n') {
      line_number_++;
    }
    return ret;
  }
  uint64_t getLineNumber() const { return line_number_; }

private:
  uint64_t line_number_;
};

/**
 * Consume events from SAX callbacks to build JSON Field.
 */
class ObjectHandler : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, ObjectHandler> {
public:
  ObjectHandler(LineCountingStringStream& stream) : state_(State::ExpectRoot), stream_(stream){};

  bool StartObject();
  bool EndObject(rapidjson::SizeType);
  bool Key(const char* value, rapidjson::SizeType size, bool);
  bool StartArray();
  bool EndArray(rapidjson::SizeType);
  bool Bool(bool value);
  bool Double(double value);
  bool Int(int value);
  bool Uint(unsigned value);
  bool Int64(int64_t value);
  bool Uint64(uint64_t value);
  bool Null();
  bool String(const char* value, rapidjson::SizeType size, bool);
  bool RawNumber(const char*, rapidjson::SizeType, bool);

  ObjectSharedPtr getRoot() { return root_; }

private:
  bool handleValueEvent(FieldSharedPtr ptr);

  enum class State {
    ExpectRoot,
    ExpectKeyOrEndObject,
    ExpectValueOrStartObjectArray,
    ExpectArrayValueOrEndArray,
    ExpectFinished,
  };
  State state_;
  LineCountingStringStream& stream_;

  std::stack<FieldSharedPtr> stack_;
  std::string key_;

  FieldSharedPtr root_;
};

void Field::buildRapidJsonDocument(const Field& field, rapidjson::Value& value,
                                   rapidjson::Document::AllocatorType& allocator) {

  switch (field.type_) {
  case Type::Array: {
    value.SetArray();
    value.Reserve(field.value_.array_value_.size(), allocator);
    for (const auto& element : field.value_.array_value_) {
      switch (element->type_) {
      case Type::Array:
      case Type::Object: {
        rapidjson::Value nested_value;
        buildRapidJsonDocument(*element, nested_value, allocator);
        value.PushBack(nested_value, allocator);
        break;
      }
      case Type::Boolean:
        value.PushBack(element->value_.boolean_value_, allocator);
        break;
      case Type::Double:
        value.PushBack(element->value_.double_value_, allocator);
        break;
      case Type::Integer:
        value.PushBack(element->value_.integer_value_, allocator);
        break;
      case Type::Null:
        value.PushBack(rapidjson::Value(), allocator);
        break;
      case Type::String:
        value.PushBack(rapidjson::StringRef(element->value_.string_value_.c_str()), allocator);
      }
    }
    break;
  }
  case Type::Object: {
    value.SetObject();
    for (const auto& item : field.value_.object_value_) {
      auto name = rapidjson::StringRef(item.first.c_str());

      switch (item.second->type_) {
      case Type::Array:
      case Type::Object: {
        rapidjson::Value nested_value;
        buildRapidJsonDocument(*item.second, nested_value, allocator);
        value.AddMember(name, nested_value, allocator);
        break;
      }
      case Type::Boolean:
        value.AddMember(name, item.second->value_.boolean_value_, allocator);
        break;
      case Type::Double:
        value.AddMember(name, item.second->value_.double_value_, allocator);
        break;
      case Type::Integer:
        value.AddMember(name, item.second->value_.integer_value_, allocator);
        break;
      case Type::Null:
        value.AddMember(name, rapidjson::Value(), allocator);
        break;
      case Type::String:
        value.AddMember(name, rapidjson::StringRef(item.second->value_.string_value_.c_str()),
                        allocator);
        break;
      }
    }
    break;
  }
  case Type::Null: {
    value.SetNull();
    break;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

rapidjson::Document Field::asRapidJsonDocument() const {
  rapidjson::Document document;
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  buildRapidJsonDocument(*this, document, allocator);
  return document;
}

uint64_t Field::hash() const { return HashUtil::xxHash64(asJsonString()); }

bool Field::getBoolean(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Boolean)) {
    throw Exception(fmt::format("key '{}' missing or not a boolean from lines {}-{}", name,
                                line_number_start_, line_number_end_));
  }
  return value_itr->second->booleanValue();
}

bool Field::getBoolean(const std::string& name, bool default_value) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getBoolean(name);
  }
  return default_value;
}

double Field::getDouble(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Double)) {
    throw Exception(fmt::format("key '{}' missing or not a double from lines {}-{}", name,
                                line_number_start_, line_number_end_));
  }
  return value_itr->second->doubleValue();
}

double Field::getDouble(const std::string& name, double default_value) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getDouble(name);
  }
  return default_value;
}

int64_t Field::getInteger(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Integer)) {
    throw Exception(fmt::format("key '{}' missing or not an integer from lines {}-{}", name,
                                line_number_start_, line_number_end_));
  }
  return value_itr->second->integerValue();
}

int64_t Field::getInteger(const std::string& name, int64_t default_value) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getInteger(name);
  }
  return default_value;
}

ObjectSharedPtr Field::getObject(const std::string& name, bool allow_empty) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end()) {
    if (allow_empty) {
      return createObject();
    } else {
      throw Exception(fmt::format("key '{}' missing from lines {}-{}", name, line_number_start_,
                                  line_number_end_));
    }
  } else if (!value_itr->second->isType(Type::Object)) {
    throw Exception(fmt::format("key '{}' not an object from line {}", name,
                                value_itr->second->line_number_start_));
  } else {
    return value_itr->second;
  }
}

std::vector<ObjectSharedPtr> Field::getObjectArray(const std::string& name,
                                                   bool allow_empty) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Array)) {
    if (allow_empty && value_itr == value_.object_value_.end()) {
      return std::vector<ObjectSharedPtr>();
    }
    throw Exception(fmt::format("key '{}' missing or not an array from lines {}-{}", name,
                                line_number_start_, line_number_end_));
  }

  std::vector<FieldSharedPtr> array_value = value_itr->second->arrayValue();
  return {array_value.begin(), array_value.end()};
}

std::string Field::getString(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::String)) {
    throw Exception(fmt::format("key '{}' missing or not a string from lines {}-{}", name,
                                line_number_start_, line_number_end_));
  }
  return value_itr->second->stringValue();
}

std::string Field::getString(const std::string& name, const std::string& default_value) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getString(name);
  }
  return default_value;
}

std::vector<std::string> Field::getStringArray(const std::string& name, bool allow_empty) const {
  checkType(Type::Object);
  std::vector<std::string> string_array;
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Array)) {
    if (allow_empty && value_itr == value_.object_value_.end()) {
      return string_array;
    }
    throw Exception(fmt::format("key '{}' missing or not an array from lines {}-{}", name,
                                line_number_start_, line_number_end_));
  }

  std::vector<FieldSharedPtr> array = value_itr->second->arrayValue();
  string_array.reserve(array.size());
  for (const auto& element : array) {
    if (!element->isType(Type::String)) {
      throw Exception(fmt::format("JSON array '{}' from line {} does not contain all strings", name,
                                  line_number_start_));
    }
    string_array.push_back(element->stringValue());
  }

  return string_array;
}

std::vector<ObjectSharedPtr> Field::asObjectArray() const {
  checkType(Type::Array);
  return {value_.array_value_.begin(), value_.array_value_.end()};
}

std::string Field::asJsonString() const {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  rapidjson::Document document = asRapidJsonDocument();
  document.Accept(writer);
  return buffer.GetString();
}

bool Field::empty() const {
  if (isType(Type::Object)) {
    return value_.object_value_.empty();
  } else if (isType(Type::Array)) {
    return value_.array_value_.empty();
  } else {
    throw Exception(
        fmt::format("Json does not support empty() on types other than array and object"));
  }
}

bool Field::hasObject(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  return value_itr != value_.object_value_.end();
}

void Field::iterate(const ObjectCallback& callback) const {
  checkType(Type::Object);
  for (const auto& item : value_.object_value_) {
    bool stop_iteration = !callback(item.first, *item.second);
    if (stop_iteration) {
      break;
    }
  }
}

void Field::validateSchema(const std::string& schema) const {
  rapidjson::Document schema_document;
  if (schema_document.Parse<0>(schema.c_str()).HasParseError()) {
    throw std::invalid_argument(fmt::format(
        "Schema supplied to validateSchema is not valid JSON\n Error(offset {}) : {}\n",
        schema_document.GetErrorOffset(), GetParseError_En(schema_document.GetParseError())));
  }

  rapidjson::SchemaDocument schema_document_for_validator(schema_document);
  rapidjson::GenericSchemaValidator<rapidjson::SchemaDocument, rapidjson::Document>
      schema_validator(schema_document_for_validator);

  if (!asRapidJsonDocument().Accept(schema_validator)) {
    rapidjson::StringBuffer schema_string_buffer;
    rapidjson::StringBuffer document_string_buffer;

    schema_validator.GetInvalidSchemaPointer().StringifyUriFragment(schema_string_buffer);
    schema_validator.GetInvalidDocumentPointer().StringifyUriFragment(document_string_buffer);

    throw Exception(fmt::format(
        "JSON at lines {}-{} does not conform to schema.\n Invalid schema: {}\n"
        " Schema violation: {}\n"
        " Offending document key: {}",
        line_number_start_, line_number_end_, schema_string_buffer.GetString(),
        schema_validator.GetInvalidSchemaKeyword(), document_string_buffer.GetString()));
  }
}

bool ObjectHandler::StartObject() {
  FieldSharedPtr object = Field::createObject();
  object->setLineNumberStart(stream_.getLineNumber());

  switch (state_) {
  case State::ExpectValueOrStartObjectArray:
    stack_.top()->insert(key_, object);
    stack_.push(object);
    state_ = State::ExpectKeyOrEndObject;
    return true;
  case State::ExpectArrayValueOrEndArray:
    stack_.top()->append(object);
    stack_.push(object);
    state_ = State::ExpectKeyOrEndObject;
    return true;
  case State::ExpectRoot:
    root_ = object;
    stack_.push(object);
    state_ = State::ExpectKeyOrEndObject;
    return true;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

bool ObjectHandler::EndObject(rapidjson::SizeType) {
  switch (state_) {
  case State::ExpectKeyOrEndObject:
    stack_.top()->setLineNumberEnd(stream_.getLineNumber());
    stack_.pop();

    if (stack_.empty()) {
      state_ = State::ExpectFinished;
    } else if (stack_.top()->isObject()) {
      state_ = State::ExpectKeyOrEndObject;
    } else if (stack_.top()->isArray()) {
      state_ = State::ExpectArrayValueOrEndArray;
    }
    return true;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

bool ObjectHandler::Key(const char* value, rapidjson::SizeType size, bool) {
  switch (state_) {
  case State::ExpectKeyOrEndObject:
    key_ = std::string(value, size);
    state_ = State::ExpectValueOrStartObjectArray;
    return true;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

bool ObjectHandler::StartArray() {
  FieldSharedPtr array = Field::createArray();
  array->setLineNumberStart(stream_.getLineNumber());

  switch (state_) {
  case State::ExpectValueOrStartObjectArray:
    stack_.top()->insert(key_, array);
    stack_.push(array);
    state_ = State::ExpectArrayValueOrEndArray;
    return true;
  case State::ExpectArrayValueOrEndArray:
    stack_.top()->append(array);
    stack_.push(array);
    return true;
  case State::ExpectRoot:
    root_ = array;
    stack_.push(array);
    state_ = State::ExpectArrayValueOrEndArray;
    return true;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

bool ObjectHandler::EndArray(rapidjson::SizeType) {
  switch (state_) {
  case State::ExpectArrayValueOrEndArray:
    stack_.top()->setLineNumberEnd(stream_.getLineNumber());
    stack_.pop();

    if (stack_.empty()) {
      state_ = State::ExpectFinished;
    } else if (stack_.top()->isObject()) {
      state_ = State::ExpectKeyOrEndObject;
    } else if (stack_.top()->isArray()) {
      state_ = State::ExpectArrayValueOrEndArray;
    }

    return true;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

// Value handlers
bool ObjectHandler::Bool(bool value) { return handleValueEvent(Field::createValue(value)); }
bool ObjectHandler::Double(double value) { return handleValueEvent(Field::createValue(value)); }
bool ObjectHandler::Int(int value) {
  return handleValueEvent(Field::createValue(static_cast<int64_t>(value)));
}
bool ObjectHandler::Uint(unsigned value) {
  return handleValueEvent(Field::createValue(static_cast<int64_t>(value)));
}
bool ObjectHandler::Int64(int64_t value) { return handleValueEvent(Field::createValue(value)); }
bool ObjectHandler::Uint64(uint64_t value) {
  if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    throw Exception(fmt::format("JSON value from line {} is larger than int64_t (not supported)",
                                stream_.getLineNumber()));
  }
  return handleValueEvent(Field::createValue(static_cast<int64_t>(value)));
}

bool ObjectHandler::Null() { return handleValueEvent(Field::createNull()); }

bool ObjectHandler::String(const char* value, rapidjson::SizeType size, bool) {
  return handleValueEvent(Field::createValue(std::string(value, size)));
}

bool ObjectHandler::RawNumber(const char*, rapidjson::SizeType, bool) {
  // Only called if kParseNumbersAsStrings is set as a parse flag, which it is not.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

bool ObjectHandler::handleValueEvent(FieldSharedPtr ptr) {
  ptr->setLineNumberStart(stream_.getLineNumber());

  switch (state_) {
  case State::ExpectValueOrStartObjectArray:
    state_ = State::ExpectKeyOrEndObject;
    stack_.top()->insert(key_, ptr);
    return true;
  case State::ExpectArrayValueOrEndArray:
    stack_.top()->append(ptr);
    return true;
  default:
    return false;
  }
}

} // namespace

ObjectSharedPtr Factory::loadFromString(const std::string& json) {
  LineCountingStringStream json_stream(json.c_str());

  ObjectHandler handler(json_stream);
  rapidjson::Reader reader;
  reader.Parse(json_stream, handler);

  if (reader.HasParseError()) {
    throw Exception(fmt::format("JSON supplied is not valid. Error(offset {}, line {}): {}\n",
                                reader.GetErrorOffset(), json_stream.getLineNumber(),
                                GetParseError_En(reader.GetParseErrorCode())));
  }

  return handler.getRoot();
}

} // namespace RapidJson
} // namespace Json
} // namespace Envoy
