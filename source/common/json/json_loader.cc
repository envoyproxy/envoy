#include "json_loader.h"

// Do not let RapidJson leak outside of this file.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include "rapidjson/error/en.h"
#include "rapidjson/istreamwrapper.h"
#include <fstream>
#include "rapidjson/document.h"
#pragma GCC diagnostic pop

namespace Json {

/**
 * Implementation of AbstractObject.
 */
class AbstractObjectImpl : public AbstractObject {
public:
  AbstractObjectImpl(const rapidjson::Document& document, const std::string& name) : name_(name) {
    document_.CopyFrom(document, document_.GetAllocator());
  }

  AbstractObjectImpl(const rapidjson::Value& value, const std::string& name) : name_(name) {
    document_.CopyFrom(value, document_.GetAllocator());
  }

  // Empty Object
  AbstractObjectImpl(const std::string& name) : name_(name) {}

  std::vector<AbstractObjectPtr> asObjectArray() const override {
    if (!document_.IsArray()) {
      throw Exception(fmt::format("'{}' is not an array", name_));
    }

    std::vector<AbstractObjectPtr> object_array;
    for (rapidjson::SizeType i = 0; i < document_.Size(); i++) {
      object_array.emplace_back(new AbstractObjectImpl(document_[i], name_ + " (array item)"));
    }

    return object_array;
  }

  bool getBoolean(const std::string& name) const override {
    if (!document_[name.c_str()].IsBool()) {
      throw Exception(fmt::format("key '{}' missing or not a boolean in '{}'", name, name_));
    }
    return document_[name.c_str()].GetBool();
  }

  bool getBoolean(const std::string& name, bool default_value) const override {
    if (!document_.HasMember(name.c_str())) {
      return default_value;
    } else {
      return getBoolean(name);
    }
  }

  int64_t getInteger(const std::string& name) const override {
    if (!document_[name.c_str()].IsInt64()) {
      throw Exception(fmt::format("key '{}' missing or not an integer in '{}'", name, name_));
    }
    return document_[name.c_str()].GetInt64();
  }

  int64_t getInteger(const std::string& name, int64_t default_value) const override {
    if (!document_.HasMember(name.c_str())) {
      return default_value;
    } else {
      return getInteger(name);
    }
  }

  AbstractObjectPtr getObject(const std::string& name, bool allow_empty) const override {
    bool object_exists = document_[name.c_str()].IsObject();
    if (allow_empty && !object_exists) {
      return AbstractObjectPtr{new AbstractObjectImpl(name)};
    } else if (!object_exists) {
      throw Exception(fmt::format("key '{}' missing in '{}'", name, name_));
    }
    return AbstractObjectPtr{new AbstractObjectImpl(document_[name.c_str()], name)};
  }

  std::vector<AbstractObjectPtr> getObjectArray(const std::string& name) const override {
    if (!document_[name.c_str()].IsArray()) {
      throw Exception(fmt::format("key '{}' missing or not an array in '{}'", name, name_));
    }

    std::vector<AbstractObjectPtr> object_array;
    for (rapidjson::SizeType i = 0; i < document_[name.c_str()].Size(); i++) {
      object_array.emplace_back(
          new AbstractObjectImpl(document_[name.c_str()][i], name + " (array item)"));
    }

    return object_array;
  }

  std::string getString(const std::string& name) const override {
    if (!document_[name.c_str()].IsString()) {
      throw Exception(fmt::format("key '{}' missing or not a string in '{}'", name, name_));
    }

    return document_[name.c_str()].GetString();
  }

  std::string getString(const std::string& name, const std::string& default_value) const override {
    if (!document_.HasMember(name.c_str())) {
      return default_value;
    } else {
      return getString(name);
    }
  }

  std::vector<std::string> getStringArray(const std::string& name) const override {
    if (!document_[name.c_str()].IsArray()) {
      throw Exception(fmt::format("key '{}' missing or not an array in '{}'", name, name_));
    }

    std::vector<std::string> string_array;
    for (rapidjson::SizeType i = 0; i < document_[name.c_str()].Size(); i++) {
      if (!document_[name.c_str()][i].IsString()) {
        throw Exception(fmt::format("array '{}' does not contain all strings", name));
      }
      string_array.push_back(document_[name.c_str()][i].GetString());
    }
    return string_array;
  }

  double getDouble(const std::string& name) const override {
    if (!document_[name.c_str()].IsDouble()) {
      throw Exception(fmt::format("key '{}' missing or not a double in '{}'", name, name_));
    }

    return document_[name.c_str()].GetDouble();
  }

  double getDouble(const std::string& name, double default_value) const override {
    if (!document_.HasMember(name.c_str())) {
      return default_value;
    } else {
      return getDouble(name);
    }
  }

  void iterate(const AbstractObjectCallback& callback) override {
    for (rapidjson::Value::ConstMemberIterator itr = document_.MemberBegin();
         itr != document_.MemberEnd(); ++itr) {
      std::string object_key(itr->name.GetString());
      AbstractObjectImpl object_value(itr->value, object_key);
      bool need_continue = callback(object_key, object_value);
      if (!need_continue) {
        break;
      }
    }
  }
  bool hasObject(const std::string& name) const override {
    return document_.HasMember(name.c_str());
  }

  std::vector<AbstractObjectPtr> getMembers() const override {
    std::vector<AbstractObjectPtr> return_vector;
    for (rapidjson::Value::ConstMemberIterator itr = document_.MemberBegin();
         itr != document_.MemberEnd(); ++itr) {
      return_vector.emplace_back(new AbstractObjectImpl(itr->value, itr->name.GetString()));
    }
    return return_vector;
  }

  const std::string getName() const override { return name_; }

private:
  std::string name_;
  rapidjson::Document document_;
};

AbstractObjectPtr Factory::LoadFromFile(const std::string& file_path) {
  rapidjson::Document document;
  std::fstream file_stream(file_path);
  rapidjson::IStreamWrapper stream_wrapper(file_stream);
  if (document.ParseStream(stream_wrapper).HasParseError()) {
    throw Exception(fmt::format("Error(offset {}): {}\n",
                                static_cast<unsigned>(document.GetErrorOffset()),
                                GetParseError_En(document.GetParseError())));
  }
  return AbstractObjectPtr{new AbstractObjectImpl(document, "root")};
}

AbstractObjectPtr Factory::LoadFromString(const std::string& json) {
  rapidjson::Document document;
  if (document.Parse<0>(json.c_str()).HasParseError()) {
    throw Exception(fmt::format("Error(offset {}): {}\n",
                                static_cast<unsigned>(document.GetErrorOffset()),
                                GetParseError_En(document.GetParseError())));
  }
  return AbstractObjectPtr{new AbstractObjectImpl(document, "root")};
}

} // Json
