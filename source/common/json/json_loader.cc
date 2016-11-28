#include "json_loader.h"

// Do not let RapidJson leak outside of this file.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/istreamwrapper.h"
#pragma GCC diagnostic pop

namespace Json {

/**
 * Implementation of AbstractObject.
 */
class AbstractObjectImplBase : public AbstractObject {
public:
  AbstractObjectImplBase(const rapidjson::Value& value, const std::string& name)
      : name_(name), value_(value) {}

  std::vector<AbstractObjectPtr> asObjectArray() const override {
    if (!value_.IsArray()) {
      throw Exception(fmt::format("'{}' is not an array", name_));
    }

    std::vector<AbstractObjectPtr> object_array;
    object_array.reserve(value_.Size());

    for (rapidjson::SizeType i = 0; i < value_.Size(); i++) {
      object_array.emplace_back(new AbstractObjectImplBase(value_[i], name_ + " (array item)"));
    }

    return object_array;
  }

  bool getBoolean(const std::string& name) const override {
    const rapidjson::Value& value = value_[name.c_str()];
    if (!value.IsBool()) {
      throw Exception(fmt::format("key '{}' missing or not a boolean in '{}'", name, name_));
    }
    return value.GetBool();
  }

  bool getBoolean(const std::string& name, bool default_value) const override {
    if (!value_.HasMember(name.c_str())) {
      return default_value;
    } else {
      return getBoolean(name);
    }
  }

  int64_t getInteger(const std::string& name) const override {
    const rapidjson::Value& value = value_[name.c_str()];
    if (!value.IsInt64()) {
      throw Exception(fmt::format("key '{}' missing or not an integer in '{}'", name, name_));
    }
    return value.GetInt64();
  }

  int64_t getInteger(const std::string& name, int64_t default_value) const override {
    if (!value_.HasMember(name.c_str())) {
      return default_value;
    } else {
      return getInteger(name);
    }
  }

  AbstractObjectPtr getObject(const std::string& name, bool allow_empty) const override {
    const rapidjson::Value& value = value_[name.c_str()];
    if (allow_empty && !value.IsObject()) {
      return AbstractObjectPtr{new AbstractObjectImplBase(empty_rapid_json_value_, name)};
    } else if (!value.IsObject()) {
      throw Exception(fmt::format("key '{}' missing in '{}'", name, name_));
    }
    return AbstractObjectPtr{new AbstractObjectImplBase(value, name)};
  }

  std::vector<AbstractObjectPtr> getObjectArray(const std::string& name) const override {
    const rapidjson::Value& value = value_[name.c_str()];
    if (!value.IsArray()) {
      throw Exception(fmt::format("key '{}' missing or not an array in '{}'", name, name_));
    }

    std::vector<AbstractObjectPtr> object_array;
    object_array.reserve(value.Size());

    for (rapidjson::SizeType i = 0; i < value.Size(); i++) {
      object_array.emplace_back(new AbstractObjectImplBase(value[i], name + " (array item)"));
    }

    return object_array;
  }

  std::string getString(const std::string& name) const override {
    const rapidjson::Value& value = value_[name.c_str()];
    if (!value.IsString()) {
      throw Exception(fmt::format("key '{}' missing or not a string in '{}'", name, name_));
    }

    return value.GetString();
  }

  std::string getString(const std::string& name, const std::string& default_value) const override {
    if (!value_.HasMember(name.c_str())) {
      return default_value;
    } else {
      return getString(name);
    }
  }

  std::vector<std::string> getStringArray(const std::string& name) const override {
    const rapidjson::Value& value = value_[name.c_str()];
    if (!value.IsArray()) {
      throw Exception(fmt::format("key '{}' missing or not an array in '{}'", name, name_));
    }

    std::vector<std::string> string_array;
    string_array.reserve(value.Size());

    for (rapidjson::SizeType i = 0; i < value.Size(); i++) {
      if (!value[i].IsString()) {
        throw Exception(fmt::format("array '{}' does not contain all strings", name));
      }
      string_array.push_back(value[i].GetString());
    }
    return string_array;
  }

  double getDouble(const std::string& name) const override {
    const rapidjson::Value& value = value_[name.c_str()];
    if (!value.IsDouble()) {
      throw Exception(fmt::format("key '{}' missing or not a double in '{}'", name, name_));
    }

    return value.GetDouble();
  }

  double getDouble(const std::string& name, double default_value) const override {
    if (!value_.HasMember(name.c_str())) {
      return default_value;
    } else {
      return getDouble(name);
    }
  }

  void iterate(const AbstractObjectCallback& callback) override {
    for (rapidjson::Value::ConstMemberIterator itr = value_.MemberBegin();
         itr != value_.MemberEnd(); ++itr) {
      std::string object_key(itr->name.GetString());
      AbstractObjectPtr abstract_object_ptr(new AbstractObjectImplBase(itr->value, object_key));
      bool need_continue = callback(object_key, abstract_object_ptr);
      if (!need_continue) {
        break;
      }
    }
  }
  bool hasObject(const std::string& name) const override { return value_.HasMember(name.c_str()); }

private:
  const std::string name_;
  const rapidjson::Value& value_;

  static const rapidjson::Value empty_rapid_json_value_;
};

const rapidjson::Value AbstractObjectImplBase::empty_rapid_json_value_;

/**
 * Holds the root AbstractObject reference.
 */
class AbstractObjectImplRoot : public AbstractObjectImplBase {
public:
  AbstractObjectImplRoot(rapidjson::Document&& document)
      : AbstractObjectImplBase(root_, "root"), root_(std::move(document)) {}

private:
  rapidjson::Document root_;
};

AbstractObjectPtr Factory::LoadFromFile(const std::string& file_path) {
  rapidjson::Document document;
  std::fstream file_stream(file_path);
  rapidjson::IStreamWrapper stream_wrapper(file_stream);
  if (document.ParseStream(stream_wrapper).HasParseError()) {
    throw Exception(fmt::format("Error(offset {}): {}\n", document.GetErrorOffset(),
                                GetParseError_En(document.GetParseError())));
  }
  return AbstractObjectPtr{new AbstractObjectImplRoot(std::move(document))};
}

AbstractObjectPtr Factory::LoadFromString(const std::string& json) {
  rapidjson::Document document;
  if (document.Parse<0>(json.c_str()).HasParseError()) {
    throw Exception(fmt::format("Error(offset {}): {}\n", document.GetErrorOffset(),
                                GetParseError_En(document.GetParseError())));
  }
  return AbstractObjectPtr{new AbstractObjectImplRoot(std::move(document))};
}

} // Json
