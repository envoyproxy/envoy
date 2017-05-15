#include "common/json/json_loader.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

#include "common/common/utility.h"
#include "common/filesystem/filesystem_impl.h"

#include "spdlog/spdlog.h"

// Do not let RapidJson leak outside of this file.
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/reader.h"
#include "rapidjson/schema.h"
#include "rapidjson/stream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Json {

class Field;
typedef std::shared_ptr<Field> FieldPtr;

class Field : public Object, public std::enable_shared_from_this<Field> {
public:
  void setLineNumberStart(uint64_t line_number) { line_number_start_ = line_number; }
  void setLineNumberEnd(uint64_t line_number) { line_number_end_ = line_number; }

  // Container factories for handler.
  static FieldPtr createObject() { return FieldPtr{new Field(Type::Object)}; }
  static FieldPtr createArray() { return FieldPtr{new Field(Type::Array)}; }
  static FieldPtr createNull() { return FieldPtr{new Field(Type::Null)}; }

  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  // Value factory.
  template <typename T> static FieldPtr createValue(T value) { return FieldPtr{new Field(value)}; }

  void append(FieldPtr field_ptr) {
    checkType(Type::Array);
    value_.array_value_.push_back(field_ptr);
  }

  void insert(const std::string& key, FieldPtr field_ptr) {
    checkType(Type::Object);
    value_.object_value_[key] = field_ptr;
  }

  // Getters.
  bool getBoolean(const std::string& name) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Boolean)) {
      throw Exception(fmt::format("key '{}' missing or not a boolean", name));
    }
    return value_itr->second->booleanValue();
  }

  bool getBoolean(const std::string& name, bool default_value) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr != value_.object_value_.end()) {
      return getBoolean(name);
    } else {
      return default_value;
    }
  }

  double getDouble(const std::string& name) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Double)) {
      throw Exception(fmt::format("key '{}' missing or not a double", name));
    }
    return value_itr->second->doubleValue();
  }

  double getDouble(const std::string& name, double default_value) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr != value_.object_value_.end()) {
      return getDouble(name);
    } else {
      return default_value;
    }
  }

  int64_t getInteger(const std::string& name) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Integer)) {
      throw Exception(fmt::format("key '{}' missing or not an integer", name));
    }
    return value_itr->second->integerValue();
  }

  int64_t getInteger(const std::string& name, int64_t default_value) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr != value_.object_value_.end()) {
      return getInteger(name);
    } else {
      return default_value;
    }
  }

  ObjectPtr getObject(const std::string& name, bool allow_empty) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr == value_.object_value_.end()) {
      if (allow_empty) {
        return createObject();
      } else {
        throw Exception(fmt::format("key '{}' missing", name));
      }
    } else if (!value_itr->second->isType(Type::Object)) {
      throw Exception(fmt::format("key '{}' not an object", name));
    } else {
      return value_itr->second;
    }
  }

  std::vector<ObjectPtr> getObjectArray(const std::string& name) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Array)) {
      throw Exception(fmt::format("key '{}' missing or not an array", name));
    }

    std::vector<FieldPtr> array_value = value_itr->second->arrayValue();
    return {array_value.begin(), array_value.end()};
  }

  std::string getString(const std::string& name) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::String)) {
      throw Exception(fmt::format("key '{}' missing or not a string", name));
    }
    return value_itr->second->stringValue();
  }

  std::string getString(const std::string& name, const std::string& default_value) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr != value_.object_value_.end()) {
      return getString(name);
    } else {
      return default_value;
    }
  }

  std::vector<std::string> getStringArray(const std::string& name) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Array)) {
      throw Exception(fmt::format("key '{}' missing or not an array", name));
    }

    std::vector<FieldPtr> array = value_itr->second->arrayValue();
    std::vector<std::string> string_array;
    string_array.reserve(array.size());
    for (const auto& element : array) {
      if (!element > isType(Type::String)) {
        throw Exception(fmt::format("array '{}' does not contain all strings", name));
      }
      string_array.push_back(element->stringValue());
    }

    return string_array;
  }

  // Helper functions.
  std::string asString() const override { return stringValue(); }

  std::vector<ObjectPtr> asObjectArray() const override {
    checkType(Type::Array);
    return {value_.array_value_.begin(), value_.array_value_.end()};
  }

  bool empty() const override {
    if (isType(Type::Object)) {
      return value_.object_value_.empty();
    } else if (isType(Type::Array)) {
      return value_.array_value_.empty();
    } else {
      throw Exception(
          fmt::format("Json does not support empty() on types other than array and object"));
    }
  }

  bool hasObject(const std::string& name) const override {
    checkType(Type::Object);
    auto value_itr = value_.object_value_.find(name);
    return value_itr != value_.object_value_.end();
  }

  void iterate(const ObjectCallback& callback) const override {
    checkType(Type::Object);
    for (const auto& item : value_.object_value_) {
      bool stop_iteration = !callback(item.first, *item.second);
      if (stop_iteration) {
        break;
      }
    }
  }

  void validateSchema(const std::string& schema) const override {
    rapidjson::Document schema_document;
    if (schema_document.Parse<0>(schema.c_str()).HasParseError()) {
      throw std::invalid_argument(fmt::format(
          "Schema supplied to validateSchema is not valid JSON\n Error(offset {}) : {}\n",
          schema_document.GetErrorOffset(), GetParseError_En(schema_document.GetParseError())));
    }

    rapidjson::SchemaDocument schema_document_for_validator(schema_document);
    rapidjson::SchemaValidator schema_validator(schema_document_for_validator);

    if (!asRapidJsonDocument().Accept(schema_validator)) {
      rapidjson::StringBuffer schema_string_buffer;
      rapidjson::StringBuffer document_string_buffer;

      schema_validator.GetInvalidSchemaPointer().StringifyUriFragment(schema_string_buffer);
      schema_validator.GetInvalidDocumentPointer().StringifyUriFragment(document_string_buffer);

      throw Exception(fmt::format(
          "JSON at lines {}-{} does not conform to schema.\n Invalid schema: {}.\n"
          " Schema violation: {}.\n"
          " Offending document key: {}",
          line_number_start_, line_number_end_, schema_string_buffer.GetString(),
          schema_validator.GetInvalidSchemaKeyword(), document_string_buffer.GetString()));
    }
  }

  rapidjson::Document asRapidJsonDocument() const {
    rapidjson::Document document;
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
    build(*this, document, allocator);
    return document;
  }

  uint64_t hash() const override {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    asRapidJsonDocument().Accept(writer);
    return std::hash<std::string>{}(buffer.GetString());
  }

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

  // Array, Object, Null ctors. Empty.
  explicit Field(Type type) : type_(type) {}

  // Type ctors.
  explicit Field(const std::string& value) {
    type_ = Type::String;
    value_.string_value_ = value;
  }

  explicit Field(int64_t value) {
    type_ = Type::Integer;
    value_.integer_value_ = value;
  }

  explicit Field(double value) {
    type_ = Type::Double;
    value_.double_value_ = value;
  }

  explicit Field(bool value) {
    type_ = Type::Boolean;
    value_.boolean_value_ = value;
  }

  void checkType(Type type) const {
    if (!isType(type)) {
      throw Exception("invalid field type");
    }
  }

  bool isType(Type type) const { return type == type_; }

  // value rtype funcs
  std::string stringValue() const {
    checkType(Type::String);
    return value_.string_value_;
  }

  std::vector<FieldPtr> arrayValue() const {
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

  static void build(const Field& field, rapidjson::Value& value,
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
          build(*element, nested_value, allocator);
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
          build(*item.second, nested_value, allocator);
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
    default:
      throw Exception("Json has a non-object or non-array at the root.");
    }
  }

  struct Value {
    std::vector<FieldPtr> array_value_;
    bool boolean_value_;
    double double_value_;
    int64_t integer_value_;
    std::map<const std::string, FieldPtr> object_value_;
    std::string string_value_;
  };

  uint64_t line_number_start_;
  uint64_t line_number_end_;
  Type type_;
  Value value_;
};

class LineCountingStringStream : public rapidjson::StringStream {

  // Ch is typdef in parent class specific to character encoding.
public:
  LineCountingStringStream(const Ch* src) : rapidjson::StringStream(src), line_number_(1) {}

  Ch Take() {
    Ch ret = rapidjson::StringStream::Take();
    if (ret == '\n') {
      line_number_++;
    }
    return ret;
  }

  uint64_t getLineNumber() { return line_number_; }

private:
  uint64_t line_number_;
};

class ObjectHandler : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, ObjectHandler> {
public:
  ObjectHandler(LineCountingStringStream& stream) : state_(expectRoot), stream_(stream){};

  bool StartObject() {
    FieldPtr object = Field::createObject();
    object->setLineNumberStart(stream_.getLineNumber());

    switch (state_) {
    case expectValueOrStartObjectArray:
      stack_.top()->insert(key_, object);
      stack_.push(object);
      state_ = expectKeyOrEndObject;
      return true;
    case expectArrayValueOrEndArray:
      stack_.top()->append(object);
      stack_.push(object);
      state_ = expectKeyOrEndObject;
      return true;
    case expectRoot:
      root_ = object;
      stack_.push(object);
      state_ = expectKeyOrEndObject;
      return true;
    default:
      return false;
    }
  }

  bool EndObject(rapidjson::SizeType) {
    switch (state_) {
    case expectKeyOrEndObject:
      stack_.top()->setLineNumberEnd(stream_.getLineNumber());
      stack_.pop();

      if (stack_.empty()) {
        state_ = expectFinished;
      } else if (stack_.top()->isObject()) {
        state_ = expectKeyOrEndObject;
      } else if (stack_.top()->isArray()) {
        state_ = expectArrayValueOrEndArray;
      }
      return true;
    default:
      return false;
    }
  }

  bool Key(const char* value, rapidjson::SizeType size, bool) {
    switch (state_) {
    case expectKeyOrEndObject:
      key_ = std::string(value, size);
      state_ = expectValueOrStartObjectArray;
      return true;
    default:
      return false;
    }
  }

  bool StartArray() {
    FieldPtr array = Field::createArray();
    array->setLineNumberStart(stream_.getLineNumber());

    switch (state_) {
    case expectValueOrStartObjectArray:
      stack_.top()->insert(key_, array);
      stack_.push(array);
      state_ = expectArrayValueOrEndArray;
      return true;
    case expectArrayValueOrEndArray:
      stack_.top()->append(array);
      stack_.push(array);
      return true;
    case expectRoot:
      root_ = array;
      stack_.push(array);
      state_ = expectArrayValueOrEndArray;
      return true;
    default:
      return false;
    }
  }

  bool EndArray(rapidjson::SizeType) {
    switch (state_) {
    case expectArrayValueOrEndArray:
      stack_.top()->setLineNumberEnd(stream_.getLineNumber());
      stack_.pop();

      if (stack_.empty()) {
        state_ = expectFinished;
      } else if (stack_.top()->isObject()) {
        state_ = expectKeyOrEndObject;
      } else if (stack_.top()->isArray()) {
        state_ = expectArrayValueOrEndArray;
      }

      return true;
    default:
      return false;
    }
  }

  // Value handlers
  bool Bool(bool value) { return handleValueEvent(Field::createValue(value)); }
  bool Double(double value) { return handleValueEvent(Field::createValue(value)); }
  bool Int(int value) { return handleValueEvent(Field::createValue(static_cast<int64_t>(value))); }
  bool Uint(unsigned value) {
    return handleValueEvent(Field::createValue(static_cast<int64_t>(value)));
  }
  bool Int64(int64_t value) { return handleValueEvent(Field::createValue(value)); }
  bool Uint64(uint64_t value) {
    if (value > std::numeric_limits<int64_t>::max()) {
      throw Exception("Json does not support numbers larger than int64_t");
    }
    return handleValueEvent(Field::createValue(static_cast<int64_t>(value)));
  }

  bool Null() { return handleValueEvent(Field::createNull()); }

  bool String(const char* value, rapidjson::SizeType size, bool) {
    return handleValueEvent(Field::createValue(std::string(value, size)));
  }

  bool RawNumber(const char*, rapidjson::SizeType, bool) {
    // Only called if kParseNumbersAsStrings is set as a parse flag, which it is not.
    return false;
  }

  ObjectPtr getRoot() { return root_; }

private:
  enum State {
    expectRoot,
    expectKeyOrEndObject,
    expectValueOrStartObjectArray,
    expectArrayValueOrEndArray,
    expectFinished,
  };
  State state_;
  LineCountingStringStream& stream_;

  std::stack<FieldPtr> stack_;
  std::string key_;

  FieldPtr root_;

  bool handleValueEvent(FieldPtr ptr) {
    switch (state_) {
    case expectValueOrStartObjectArray:
      state_ = expectKeyOrEndObject;
      stack_.top()->insert(key_, ptr);
      return true;
    case expectArrayValueOrEndArray:
      stack_.top()->append(ptr);
      return true;
    default:
      return false;
    }
  }
};

ObjectPtr Factory::loadFromFile(const std::string& file_path) {
  return loadFromString(Filesystem::fileReadToEnd(file_path));
}

ObjectPtr Factory::loadFromString(const std::string& json) {
  LineCountingStringStream json_stream(json.c_str());

  ObjectHandler handler(json_stream);
  rapidjson::Reader reader;
  reader.Parse(json_stream, handler);

  if (reader.HasParseError()) {
    throw Exception(fmt::format("Error(offset {}): {}\n", reader.GetErrorOffset(),
                                GetParseError_En(reader.GetParseErrorCode())));
  }

  return handler.getRoot();
}

const std::string Factory::listAsJsonString(const std::list<std::string>& items) {
  rapidjson::StringBuffer writer_string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(writer_string_buffer);

  writer.StartArray();
  for (const std::string& item : items) {
    writer.String(item.c_str());
  }
  writer.EndArray();

  return writer_string_buffer.GetString();
}

} // Json
