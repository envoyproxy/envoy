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

// Do not let RapidJson leak outside of json_loader.
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/reader.h"
#include "rapidjson/schema.h"
#include "rapidjson/stream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Json {

ObjectPtr Factory::loadFromFile(const std::string& file_path) {
  return loadFromString(Filesystem::fileReadToEnd(file_path));
}

ObjectPtr Factory::loadFromString(const std::string& json) {
  Factory::LineCountingStringStream json_stream(json.c_str());

  Factory::ObjectHandler handler(json_stream);
  rapidjson::Reader reader;
  reader.Parse(json_stream, handler);

  if (reader.HasParseError()) {
    throw Exception(fmt::format("JSON supplied is not valid. Error(offset {}): {}\n",
                                reader.GetErrorOffset(),
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

void Factory::Field::build(const Field& field, rapidjson::Value& value,
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

rapidjson::Document Factory::Field::asRapidJsonDocument() const {
  rapidjson::Document document;
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  build(*this, document, allocator);
  return document;
}

uint64_t Factory::Field::hash() const {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  asRapidJsonDocument().Accept(writer);
  return std::hash<std::string>{}(buffer.GetString());
}

bool Factory::Field::getBoolean(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Boolean)) {
    throw Exception(fmt::format("key '{}' missing or not a boolean", name));
  }
  return value_itr->second->booleanValue();
}

bool Factory::Field::getBoolean(const std::string& name, bool default_value) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getBoolean(name);
  } else {
    return default_value;
  }
}

double Factory::Field::getDouble(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Double)) {
    throw Exception(fmt::format("key '{}' missing or not a double", name));
  }
  return value_itr->second->doubleValue();
}

double Factory::Field::getDouble(const std::string& name, double default_value) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getDouble(name);
  } else {
    return default_value;
  }
}

int64_t Factory::Field::getInteger(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Integer)) {
    throw Exception(fmt::format("key '{}' missing or not an integer", name));
  }
  return value_itr->second->integerValue();
}

int64_t Factory::Field::getInteger(const std::string& name, int64_t default_value) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getInteger(name);
  } else {
    return default_value;
  }
}

ObjectPtr Factory::Field::getObject(const std::string& name, bool allow_empty) const {
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

std::vector<ObjectPtr> Factory::Field::getObjectArray(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Array)) {
    throw Exception(fmt::format("key '{}' missing or not an array", name));
  }

  std::vector<FieldPtr> array_value = value_itr->second->arrayValue();
  return {array_value.begin(), array_value.end()};
}

std::string Factory::Field::getString(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::String)) {
    throw Exception(fmt::format("key '{}' missing or not a string", name));
  }
  return value_itr->second->stringValue();
}

std::string Factory::Field::getString(const std::string& name,
                                      const std::string& default_value) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getString(name);
  } else {
    return default_value;
  }
}

std::vector<std::string> Factory::Field::getStringArray(const std::string& name) const {
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

std::vector<ObjectPtr> Factory::Field::asObjectArray() const {
  checkType(Type::Array);
  return {value_.array_value_.begin(), value_.array_value_.end()};
}

bool Factory::Field::empty() const {
  if (isType(Type::Object)) {
    return value_.object_value_.empty();
  } else if (isType(Type::Array)) {
    return value_.array_value_.empty();
  } else {
    throw Exception(
        fmt::format("Json does not support empty() on types other than array and object"));
  }
}

bool Factory::Field::hasObject(const std::string& name) const {
  checkType(Type::Object);
  auto value_itr = value_.object_value_.find(name);
  return value_itr != value_.object_value_.end();
}

void Factory::Field::iterate(const ObjectCallback& callback) const {
  checkType(Type::Object);
  for (const auto& item : value_.object_value_) {
    bool stop_iteration = !callback(item.first, *item.second);
    if (stop_iteration) {
      break;
    }
  }
}

void Factory::Field::validateSchema(const std::string& schema) const {
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

bool Factory::ObjectHandler::StartObject() {
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

bool Factory::ObjectHandler::EndObject(rapidjson::SizeType) {
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

bool Factory::ObjectHandler::Key(const char* value, rapidjson::SizeType size, bool) {
  switch (state_) {
  case expectKeyOrEndObject:
    key_ = std::string(value, size);
    state_ = expectValueOrStartObjectArray;
    return true;
  default:
    return false;
  }
}

bool Factory::ObjectHandler::StartArray() {
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

bool Factory::ObjectHandler::EndArray(rapidjson::SizeType) {
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
bool Factory::ObjectHandler::Bool(bool value) {
  return handleValueEvent(Field::createValue(value));
}
bool Factory::ObjectHandler::Double(double value) {
  return handleValueEvent(Field::createValue(value));
}
bool Factory::ObjectHandler::Int(int value) {
  return handleValueEvent(Field::createValue(static_cast<int64_t>(value)));
}
bool Factory::ObjectHandler::Uint(unsigned value) {
  return handleValueEvent(Field::createValue(static_cast<int64_t>(value)));
}
bool Factory::ObjectHandler::Int64(int64_t value) {
  return handleValueEvent(Field::createValue(value));
}
bool Factory::ObjectHandler::Uint64(uint64_t value) {
  if (value > std::numeric_limits<int64_t>::max()) {
    throw Exception("Json does not support numbers larger than int64_t");
  }
  return handleValueEvent(Field::createValue(static_cast<int64_t>(value)));
}

bool Factory::ObjectHandler::Null() { return handleValueEvent(Field::createNull()); }

bool Factory::ObjectHandler::String(const char* value, rapidjson::SizeType size, bool) {
  return handleValueEvent(Field::createValue(std::string(value, size)));
}

bool Factory::ObjectHandler::RawNumber(const char*, rapidjson::SizeType, bool) {
  // Only called if kParseNumbersAsStrings is set as a parse flag, which it is not.
  return false;
}

bool Factory::ObjectHandler::handleValueEvent(FieldPtr ptr) {
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

} // Json
