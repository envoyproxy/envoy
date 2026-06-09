#include "source/common/json/json_internal.h"

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

// Do not let nlohmann/json leak outside of this file.
#include "absl/strings/match.h"
#include "include/nlohmann/json.hpp"

namespace Envoy {
namespace Json {
namespace Nlohmann {

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

  absl::Status append(FieldSharedPtr field_ptr) {
    RETURN_IF_NOT_OK(checkType(Type::Array));
    value_.array_value_.push_back(field_ptr);
    return absl::OkStatus();
  }
  absl::Status insert(const std::string& key, FieldSharedPtr field_ptr) {
    RETURN_IF_NOT_OK(checkType(Type::Object));
    value_.object_value_[key] = field_ptr;
    return absl::OkStatus();
  }

  uint64_t hash() const override;

  absl::StatusOr<ValueType> getValue(const std::string& name) const override;
  absl::StatusOr<bool> getBoolean(const std::string& name) const override;
  absl::StatusOr<bool> getBoolean(const std::string& name, bool default_value) const override;
  absl::StatusOr<double> getDouble(const std::string& name) const override;
  absl::StatusOr<double> getDouble(const std::string& name, double default_value) const override;
  absl::StatusOr<int64_t> getInteger(const std::string& name) const override;
  absl::StatusOr<int64_t> getInteger(const std::string& name, int64_t default_value) const override;
  absl::StatusOr<ObjectSharedPtr> getObject(const std::string& name,
                                            bool allow_empty) const override;
  absl::StatusOr<std::vector<ObjectSharedPtr>> getObjectArray(const std::string& name,
                                                              bool allow_empty) const override;
  absl::StatusOr<std::string> getString(const std::string& name) const override;
  absl::StatusOr<std::string> getString(const std::string& name,
                                        const std::string& default_value) const override;
  absl::StatusOr<std::vector<std::string>> getStringArray(const std::string& name,
                                                          bool allow_empty) const override;
  absl::StatusOr<std::vector<ObjectSharedPtr>> asObjectArray() const override;
  absl::StatusOr<std::string> asString() const override { return stringValue(); }
  std::string asJsonString() const override;

  bool empty() const override;
  bool hasObject(const std::string& name) const override;
  absl::Status iterate(const ObjectCallback& callback) const override;
  void validateSchema(const std::string&) const override;

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

    return "";
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
  absl::Status checkType(Type type) const {
    if (!isType(type)) {
      return absl::InvalidArgumentError(fmt::format(
          "JSON field from line {} accessed with type '{}' does not match actual type '{}'.",
          line_number_start_, typeAsString(type), typeAsString(type_)));
    }
    return absl::OkStatus();
  }

  // Value return type functions.
  absl::StatusOr<std::string> stringValue() const {
    RETURN_IF_NOT_OK(checkType(Type::String));
    return value_.string_value_;
  }
  absl::StatusOr<std::vector<FieldSharedPtr>> arrayValue() const {
    RETURN_IF_NOT_OK(checkType(Type::Array));
    return value_.array_value_;
  }
  absl::StatusOr<bool> booleanValue() const {
    RETURN_IF_NOT_OK(checkType(Type::Boolean));
    return value_.boolean_value_;
  }
  absl::StatusOr<double> doubleValue() const {
    RETURN_IF_NOT_OK(checkType(Type::Double));
    return value_.double_value_;
  }
  absl::StatusOr<int64_t> integerValue() const {
    RETURN_IF_NOT_OK(checkType(Type::Integer));
    return value_.integer_value_;
  }

  nlohmann::json asJsonDocument() const;
  static void buildJsonDocument(const Field& field, nlohmann::json& value);

  uint64_t line_number_start_ = 0;
  uint64_t line_number_end_ = 0;
  const Type type_;
  Value value_;
};

/**
 * Consume events from SAX callbacks to build JSON Field.
 */
class ObjectHandler : public nlohmann::json_sax<nlohmann::json> {
public:
  ObjectHandler() = default;

  bool start_object(std::size_t) override;
  bool end_object() override;
  bool key(std::string& val) override;
  bool start_array(std::size_t) override;
  bool end_array() override;
  bool boolean(bool value) override { return handleValueEvent(Field::createValue(value)); }
  bool number_integer(int64_t value) override {
    return handleValueEvent(Field::createValue(static_cast<int64_t>(value)));
  }
  bool number_unsigned(uint64_t value) override {
    if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      // Envoy's code is discouraging the use of exceptions. The following sets
      // the error details as if an exception occurs, and returns false to stop
      // parsing.
      error_ = fmt::format("JSON value from line {} is larger than int64_t (not supported)",
                           line_number_);
      error_position_ = absl::StrCat("line: ", line_number_);
      return false;
    }
    return handleValueEvent(Field::createValue(static_cast<int64_t>(value)));
  }
  bool number_float(double value, const std::string&) override {
    return handleValueEvent(Field::createValue(value));
  }
  bool null() override { return handleValueEvent(Field::createNull()); }
  bool string(std::string& value) override { return handleValueEvent(Field::createValue(value)); }
  bool binary(binary_t&) override { return false; }
  bool parse_error(std::size_t at, const std::string& token,
                   const nlohmann::detail::exception& ex) override {
    // Parse errors are formatted like "[json.exception.parse_error.101] parse error: explanatory
    // string." or "[json.exception.parse_error.101] parser error at (position): explanatory
    // string.". All errors start with "[json.exception.<error_type>.<error_num]" see:
    // https://json.nlohmann.me/home/exceptions/#parse-errors
    // The `parse_error` method will be called also for non-parse errors.
    absl::string_view error = ex.what();

    // Colon will always exist in the parse error. For non-parse error use the
    // ending ']' as a separator.
    auto end = error.find(": ");
    auto prefix_end = error.find(']');
    if (end != std::string::npos) {
      // Extract portion after ": " to get error string.
      error_ = std::string(error.substr(end + 2));
      // Extract position information if present.
      auto start = error.find("at ");
      if (start != std::string::npos && (start + 3) < end) {
        start += 3;
        error_position_ = absl::StrCat(error.substr(start, end - start), ", token ", token);
      }
    } else if ((prefix_end != std::string::npos) && (absl::StartsWith(error, ErrorPrefix))) {
      // Non-parse error, fetching position from the arguments as it is not
      // present in the error string.
      error_position_ = absl::StrCat("position: ", at);
      error_ = std::string(error.substr(prefix_end + 1));
    } else {
      IS_ENVOY_BUG("Error string not present. Check nlohmann/json "
                   "documentation in case error string changed.");
    }
    return false;
  }

  bool hasParseError() { return !error_.empty(); }
  std::string getParseError() { return error_; }
  std::string getErrorPosition() { return error_position_; }

  ObjectSharedPtr getRoot() { return root_; }

  int line_number_{1};

private:
  bool handleValueEvent(FieldSharedPtr ptr);

  enum class State {
    ExpectRoot,
    ExpectKeyOrEndObject,
    ExpectValueOrStartObjectArray,
    ExpectArrayValueOrEndArray,
    ExpectFinished,
  };
  State state_{State::ExpectRoot};

  std::stack<FieldSharedPtr> stack_;
  std::string key_;

  FieldSharedPtr root_;

  std::string error_;
  std::string error_position_;

  static constexpr absl::string_view ErrorPrefix = "[json.exception.";
};

struct JsonContainer {
  JsonContainer(const char* ch, ObjectHandler* handler) : data(ch), handler_(handler) {}
  const char* data;
  ObjectHandler* handler_;
};

struct JsonIterator {
  using difference_type = std::ptrdiff_t;            // NOLINT(readability-identifier-naming)
  using value_type = char;                           // NOLINT(readability-identifier-naming)
  using pointer = const char*;                       // NOLINT(readability-identifier-naming)
  using reference = const char&;                     // NOLINT(readability-identifier-naming)
  using iterator_category = std::input_iterator_tag; // NOLINT(readability-identifier-naming)

  JsonIterator& operator++() {
    ++ptr.data;
    return *this;
  }

  bool operator!=(const JsonIterator& rhs) const { return rhs.ptr.data != ptr.data; }

  reference operator*() {
    const char& ch = *(ptr.data);
    if (ch == '\n') {
      ptr.handler_->line_number_++;
    }
    return ch;
  }

  JsonContainer ptr;
};

JsonIterator begin(const JsonContainer& c) {
  return JsonIterator{JsonContainer(c.data, c.handler_)};
}

JsonIterator end(const JsonContainer& c) {
  return JsonIterator{JsonContainer(c.data + strlen(c.data), c.handler_)};
}

void Field::buildJsonDocument(const Field& field, nlohmann::json& value) {
  switch (field.type_) {
  case Type::Array: {
    for (const auto& element : field.value_.array_value_) {
      switch (element->type_) {
      case Type::Array:
      case Type::Object: {
        nlohmann::json nested_value;
        buildJsonDocument(*element, nested_value);
        value.push_back(nested_value);
        break;
      }
      case Type::Boolean:
        value.push_back(element->value_.boolean_value_);
        break;
      case Type::Double:
        value.push_back(element->value_.double_value_);
        break;
      case Type::Integer:
        value.push_back(element->value_.integer_value_);
        break;
      case Type::Null:
        value.push_back(nlohmann::json::value_t::null);
        break;
      case Type::String:
        value.push_back(element->value_.string_value_);
      }
    }
    break;
  }
  case Type::Object: {
    for (const auto& item : field.value_.object_value_) {
      auto name = std::string(item.first);

      switch (item.second->type_) {
      case Type::Array:
      case Type::Object: {
        nlohmann::json nested_value;
        buildJsonDocument(*item.second, nested_value);
        value.emplace(name, nested_value);
        break;
      }
      case Type::Boolean:
        value.emplace(name, item.second->value_.boolean_value_);
        break;
      case Type::Double:
        value.emplace(name, item.second->value_.double_value_);
        break;
      case Type::Integer:
        value.emplace(name, item.second->value_.integer_value_);
        break;
      case Type::Null:
        value.emplace(name, nlohmann::json::value_t::null);
        break;
      case Type::String:
        value.emplace(name, item.second->value_.string_value_);
        break;
      }
    }
    break;
  }
  case Type::Null: {
    break;
  }
  case Type::Boolean:
    FALLTHRU;
  case Type::Double:
    FALLTHRU;
  case Type::Integer:
    FALLTHRU;
  case Type::String:
    PANIC("not implemented");
  }
}

nlohmann::json Field::asJsonDocument() const {
  nlohmann::json j;
  buildJsonDocument(*this, j);
  return j;
}

uint64_t Field::hash() const { return HashUtil::xxHash64(asJsonString()); }

absl::StatusOr<ValueType> Field::getValue(const std::string& name) const {
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end()) {
    return absl::NotFoundError(fmt::format("key '{}' missing from lines {}-{}", name,
                                           line_number_start_, line_number_end_));
  }
  switch (value_itr->second->type_) {
  case Type::Boolean:
    return value_itr->second->booleanValue();
  case Type::Double:
    return value_itr->second->doubleValue();
  case Type::Integer:
    return value_itr->second->integerValue();
  case Type::String:
    return value_itr->second->stringValue();
  default:
    return absl::InternalError(fmt::format("key '{}' not a value type from lines {}-{}", name,
                                           line_number_start_, line_number_end_));
  }
}

absl::StatusOr<bool> Field::getBoolean(const std::string& name) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Boolean)) {
    return absl::InvalidArgumentError(
        fmt::format("key '{}' missing or not a boolean from lines {}-{}", name, line_number_start_,
                    line_number_end_));
  }
  return value_itr->second->booleanValue();
}

absl::StatusOr<bool> Field::getBoolean(const std::string& name, bool default_value) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getBoolean(name);
  }
  return default_value;
}

absl::StatusOr<double> Field::getDouble(const std::string& name) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Double)) {
    return absl::InvalidArgumentError(
        fmt::format("key '{}' missing or not a double from lines {}-{}", name, line_number_start_,
                    line_number_end_));
  }
  return value_itr->second->doubleValue();
}

absl::StatusOr<double> Field::getDouble(const std::string& name, double default_value) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getDouble(name);
  }
  return default_value;
}

absl::StatusOr<int64_t> Field::getInteger(const std::string& name) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Integer)) {
    return absl::InvalidArgumentError(
        fmt::format("key '{}' missing or not an integer from lines {}-{}", name, line_number_start_,
                    line_number_end_));
  }
  return value_itr->second->integerValue();
}

absl::StatusOr<int64_t> Field::getInteger(const std::string& name, int64_t default_value) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getInteger(name);
  }
  return default_value;
}

absl::StatusOr<ObjectSharedPtr> Field::getObject(const std::string& name, bool allow_empty) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end()) {
    if (allow_empty) {
      return createObject();
    } else {
      return absl::NotFoundError(fmt::format("key '{}' missing from lines {}-{}", name,
                                             line_number_start_, line_number_end_));
    }
  } else if (!value_itr->second->isType(Type::Object)) {
    return absl::InternalError(fmt::format("key '{}' not an object from line {}", name,
                                           value_itr->second->line_number_start_));
  }

  return value_itr->second;
}

absl::StatusOr<std::vector<ObjectSharedPtr>> Field::getObjectArray(const std::string& name,
                                                                   bool allow_empty) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Array)) {
    if (allow_empty && value_itr == value_.object_value_.end()) {
      std::vector<ObjectSharedPtr> ret;
      return ret;
    }
    return absl::InvalidArgumentError(
        fmt::format("key '{}' missing or not an array from lines {}-{}", name, line_number_start_,
                    line_number_end_));
  }

  auto array_value_or_error = value_itr->second->arrayValue();
  RETURN_IF_NOT_OK_REF(array_value_or_error.status());
  std::vector<FieldSharedPtr>& array_value = array_value_or_error.value();
  std::vector<ObjectSharedPtr> ret{array_value.begin(), array_value.end()};
  return ret;
}

absl::StatusOr<std::string> Field::getString(const std::string& name) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::String)) {
    return absl::InvalidArgumentError(
        fmt::format("key '{}' missing or not a string from lines {}-{}", name, line_number_start_,
                    line_number_end_));
  }
  return value_itr->second->stringValue();
}

absl::StatusOr<std::string> Field::getString(const std::string& name,
                                             const std::string& default_value) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  auto value_itr = value_.object_value_.find(name);
  if (value_itr != value_.object_value_.end()) {
    return getString(name);
  }
  return default_value;
}

absl::StatusOr<std::vector<std::string>> Field::getStringArray(const std::string& name,
                                                               bool allow_empty) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  std::vector<std::string> string_array;
  auto value_itr = value_.object_value_.find(name);
  if (value_itr == value_.object_value_.end() || !value_itr->second->isType(Type::Array)) {
    if (allow_empty && value_itr == value_.object_value_.end()) {
      return string_array;
    }
    return absl::InvalidArgumentError(
        fmt::format("key '{}' missing or not an array from lines {}-{}", name, line_number_start_,
                    line_number_end_));
  }

  auto array_value_or_error = value_itr->second->arrayValue();
  RETURN_IF_NOT_OK_REF(array_value_or_error.status());
  std::vector<FieldSharedPtr>& array = array_value_or_error.value();
  string_array.reserve(array.size());
  for (const auto& element : array) {
    if (!element->isType(Type::String)) {
      return absl::InvalidArgumentError(fmt::format(
          "JSON array '{}' from line {} does not contain all strings", name, line_number_start_));
    }
    RETURN_IF_NOT_OK_REF(element->stringValue().status());
    string_array.push_back(element->stringValue().value());
  }

  return string_array;
}

absl::StatusOr<std::vector<ObjectSharedPtr>> Field::asObjectArray() const {
  RETURN_IF_NOT_OK(checkType(Type::Array));
  return std::vector<ObjectSharedPtr>{value_.array_value_.begin(), value_.array_value_.end()};
}

std::string Field::asJsonString() const {
  nlohmann::json j = asJsonDocument();
  // Call with defaults except in the case of UTF-8 errors which we replace
  // invalid UTF-8 characters instead of throwing an exception.
  return j.dump(-1, ' ', false, nlohmann::detail::error_handler_t::replace);
}

bool Field::empty() const {
  if (isType(Type::Object)) {
    return value_.object_value_.empty();
  } else if (isType(Type::Array)) {
    return value_.array_value_.empty();
  }
  IS_ENVOY_BUG("Json does not support empty() on types other than array and object");
  return false;
}

bool Field::hasObject(const std::string& name) const {
  if (!checkType(Type::Object).ok()) {
    IS_ENVOY_BUG("HasObject called on non-object");
    return false;
  }
  auto value_itr = value_.object_value_.find(name);
  return value_itr != value_.object_value_.end();
}

absl::Status Field::iterate(const ObjectCallback& callback) const {
  RETURN_IF_NOT_OK(checkType(Type::Object));
  for (const auto& item : value_.object_value_) {
    bool stop_iteration = !callback(item.first, *item.second);
    if (stop_iteration) {
      break;
    }
  }
  return absl::OkStatus();
}

void Field::validateSchema(const std::string&) const {
  IS_ENVOY_BUG("validateSchema not implemented");
}

bool ObjectHandler::start_object(std::size_t) {
  FieldSharedPtr object = Field::createObject();
  object->setLineNumberStart(line_number_);

  switch (state_) {
  case State::ExpectValueOrStartObjectArray:
    THROW_IF_NOT_OK(stack_.top()->insert(key_, object));
    stack_.push(object);
    state_ = State::ExpectKeyOrEndObject;
    return true;
  case State::ExpectArrayValueOrEndArray:
    THROW_IF_NOT_OK(stack_.top()->append(object));
    stack_.push(object);
    state_ = State::ExpectKeyOrEndObject;
    return true;
  case State::ExpectRoot:
    root_ = object;
    stack_.push(object);
    state_ = State::ExpectKeyOrEndObject;
    return true;
  case State::ExpectKeyOrEndObject:
    FALLTHRU;
  case State::ExpectFinished:
    PANIC("not implemented");
  }
  return false;
}

bool ObjectHandler::end_object() {
  if (state_ == State::ExpectKeyOrEndObject) {
    stack_.top()->setLineNumberEnd(line_number_);
    stack_.pop();

    if (stack_.empty()) {
      state_ = State::ExpectFinished;
    } else if (stack_.top()->isObject()) {
      state_ = State::ExpectKeyOrEndObject;
    } else if (stack_.top()->isArray()) {
      state_ = State::ExpectArrayValueOrEndArray;
    }
    return true;
  }
  PANIC("parsing error not handled");
}

bool ObjectHandler::key(std::string& val) {
  if (state_ == State::ExpectKeyOrEndObject) {
    key_ = val;
    state_ = State::ExpectValueOrStartObjectArray;
    return true;
  }
  PANIC("parsing error not handled");
}

bool ObjectHandler::start_array(std::size_t) {
  FieldSharedPtr array = Field::createArray();
  array->setLineNumberStart(line_number_);

  switch (state_) {
  case State::ExpectValueOrStartObjectArray:
    THROW_IF_NOT_OK(stack_.top()->insert(key_, array));
    stack_.push(array);
    state_ = State::ExpectArrayValueOrEndArray;
    return true;
  case State::ExpectArrayValueOrEndArray:
    THROW_IF_NOT_OK(stack_.top()->append(array));
    stack_.push(array);
    return true;
  case State::ExpectRoot:
    root_ = array;
    stack_.push(array);
    state_ = State::ExpectArrayValueOrEndArray;
    return true;
  default:
    PANIC("parsing error not handled");
  }
}

bool ObjectHandler::end_array() {
  switch (state_) {
  case State::ExpectArrayValueOrEndArray:
    stack_.top()->setLineNumberEnd(line_number_);
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
    PANIC("parsing error not handled");
  }
}

bool ObjectHandler::handleValueEvent(FieldSharedPtr ptr) {
  ptr->setLineNumberStart(line_number_);

  switch (state_) {
  case State::ExpectValueOrStartObjectArray:
    state_ = State::ExpectKeyOrEndObject;
    THROW_IF_NOT_OK(stack_.top()->insert(key_, ptr));
    return true;
  case State::ExpectArrayValueOrEndArray:
    THROW_IF_NOT_OK(stack_.top()->append(ptr));
    return true;
  default:
    return true;
  }
}

} // namespace

absl::StatusOr<ObjectSharedPtr> Factory::loadFromString(const std::string& json) {
  ObjectHandler handler;
  auto json_container = JsonContainer(json.c_str(), &handler);

  nlohmann::json::sax_parse(json_container, &handler);

  if (handler.hasParseError()) {
    return absl::InternalError(fmt::format("JSON supplied is not valid. Error({}): {}\n",
                                           handler.getErrorPosition(), handler.getParseError()));
  }
  return handler.getRoot();
}

absl::StatusOr<FieldSharedPtr>
loadFromProtobufStructInternal(const Protobuf::Struct& protobuf_struct);

absl::StatusOr<FieldSharedPtr>
loadFromProtobufValueInternal(const Protobuf::Value& protobuf_value) {
  switch (protobuf_value.kind_case()) {
  case Protobuf::Value::kStringValue:
    return Field::createValue(protobuf_value.string_value());
  case Protobuf::Value::kNumberValue:
    return Field::createValue(protobuf_value.number_value());
  case Protobuf::Value::kBoolValue:
    return Field::createValue(protobuf_value.bool_value());
  case Protobuf::Value::kNullValue:
    return Field::createNull();
  case Protobuf::Value::kListValue: {
    FieldSharedPtr array = Field::createArray();
    for (const auto& list_value : protobuf_value.list_value().values()) {
      absl::StatusOr<FieldSharedPtr> proto_or_error = loadFromProtobufValueInternal(list_value);
      RETURN_IF_NOT_OK_REF(proto_or_error.status());
      RETURN_IF_NOT_OK(array->append(*proto_or_error));
    }
    return array;
  }
  case Protobuf::Value::kStructValue:
    return loadFromProtobufStructInternal(protobuf_value.struct_value());
  case Protobuf::Value::KIND_NOT_SET:
    break;
  }
  return absl::InvalidArgumentError("Protobuf value case not implemented");
}

absl::StatusOr<FieldSharedPtr>
loadFromProtobufStructInternal(const Protobuf::Struct& protobuf_struct) {
  auto root = Field::createObject();
  for (const auto& field : protobuf_struct.fields()) {
    absl::StatusOr<FieldSharedPtr> proto_or_error = loadFromProtobufValueInternal(field.second);
    RETURN_IF_NOT_OK_REF(proto_or_error.status());
    RETURN_IF_NOT_OK(root->insert(field.first, *proto_or_error));
  }

  return root;
}

ObjectSharedPtr Factory::loadFromProtobufStruct(const Protobuf::Struct& protobuf_struct) {
  return THROW_OR_RETURN_VALUE(loadFromProtobufStructInternal(protobuf_struct), ObjectSharedPtr);
}

std::string Factory::serialize(absl::string_view str) {
  nlohmann::json j(str);
  return j.dump(-1, ' ', false, nlohmann::detail::error_handler_t::replace);
}

template <typename T> std::string Factory::serialize(const T& items) {
  nlohmann::json j = nlohmann::json(items);
  return j.dump();
}

std::vector<uint8_t> Factory::jsonToMsgpack(const std::string& json_string) {
  return nlohmann::json::to_msgpack(nlohmann::json::parse(json_string, nullptr, false));
}

// Template instantiation for serialize function.
template std::string Factory::serialize(const std::list<std::string>& items);
template std::string Factory::serialize(const absl::flat_hash_set<std::string>& items);
template std::string Factory::serialize(
    const absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, int>>& items);

} // namespace Nlohmann
} // namespace Json
} // namespace Envoy
