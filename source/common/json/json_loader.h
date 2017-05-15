#pragma once

#include <list>
#include <map>
#include <stack>
#include <string>

#include "envoy/json/json_object.h"

// Do not let RapidJson leak outside of this file.
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/reader.h"
#include "rapidjson/schema.h"
#include "rapidjson/stream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Json {

class Factory {
public:
  /*
   * Constructs a Json Object from a File.
   */
  static ObjectPtr loadFromFile(const std::string& file_path);
  // static ObjectPtr loadFromFileTwo(const std::string& file_path);

  /*
   * Constructs a Json Object from a String.
   */
  static ObjectPtr loadFromString(const std::string& json);

  static const std::string listAsJsonString(const std::list<std::string>& items);

private:
  /*
   * Internal representation of Object.
   */
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
    template <typename T> static FieldPtr createValue(T value) {
      return FieldPtr{new Field(value)};
    }

    void append(FieldPtr field_ptr) {
      checkType(Type::Array);
      value_.array_value_.push_back(field_ptr);
    }
    void insert(const std::string& key, FieldPtr field_ptr) {
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
    ObjectPtr getObject(const std::string& name, bool allow_empty) const override;
    std::vector<ObjectPtr> getObjectArray(const std::string& name) const override;
    std::string getString(const std::string& name) const override;
    std::string getString(const std::string& name, const std::string& default_value) const override;
    std::vector<std::string> getStringArray(const std::string& name) const override;
    std::vector<ObjectPtr> asObjectArray() const override;
    std::string asString() const override { return stringValue(); }

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

    struct Value {
      std::vector<FieldPtr> array_value_;
      bool boolean_value_;
      double double_value_;
      int64_t integer_value_;
      std::map<const std::string, FieldPtr> object_value_;
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
        throw Exception("invalid field type");
      }
    }
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

    rapidjson::Document asRapidJsonDocument() const;
    static void build(const Field& field, rapidjson::Value& value,
                      rapidjson::Document::AllocatorType& allocator);

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

  /*
   * SAX domain parser.
   */
  class ObjectHandler : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, ObjectHandler> {
  public:
    ObjectHandler(Factory::LineCountingStringStream& stream)
        : state_(expectRoot), stream_(stream){};

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

    ObjectPtr getRoot() { return root_; }

  private:
    bool handleValueEvent(FieldPtr ptr);

    enum State {
      expectRoot,
      expectKeyOrEndObject,
      expectValueOrStartObjectArray,
      expectArrayValueOrEndArray,
      expectFinished,
    };
    State state_;
    LineCountingStringStream& stream_;

    std::stack<Factory::FieldPtr> stack_;
    std::string key_;

    FieldPtr root_;
  };
};

} // Json
