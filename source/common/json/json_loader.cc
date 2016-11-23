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
class AbstractObjectImpl : public AbstractObject {
public:
  AbstractObjectImpl(std::shared_ptr<rapidjson::Value> value, const std::string& name)
      : name_(name), value_(value) {}

  AbstractObjectImpl(const std::string& name) : name_(name) {
    value_ = std::make_shared<rapidjson::Value>();
  }
  std::vector<AbstractObjectPtr> asObjectArray() const override {
    if (!value_->IsArray()) {
      throw Exception(fmt::format("'{}' is not an array", name_));
    }

    std::vector<AbstractObjectPtr> object_array;
    for (rapidjson::SizeType i = 0; i < value_->Size(); i++) {
      //  object_array.emplace_back(new AbstractObjectImpl(value_[i], name_ + " (array item)"));
      object_array.emplace_back(new AbstractObjectImpl(name_ + " (array item)"));
    }

    return object_array;
  }

  bool getBoolean(const std::string& name) const override {
    const rapidjson::Value& value = (*value_.get())[name.c_str()];
    if (!value.IsBool()) {
      throw Exception(fmt::format("key '{}' missing or not a boolean in '{}'", name, name_));
    }
    return value.GetBool();
  }

  bool getBoolean(const std::string& name, bool default_value) const override {
    if (!value_.get()->HasMember(name.c_str())) {
      return default_value;
    } else {
      return getBoolean(name);
    }
  }

  int64_t getInteger(const std::string& name) const override {
    const rapidjson::Value& value = (*value_.get())[name.c_str()];
    if (!value.IsInt64()) {
      throw Exception(fmt::format("key '{}' missing or not an integer in '{}'", name, name_));
    }
    return value.GetInt64();
  }

  int64_t getInteger(const std::string& name, int64_t default_value) const override {
    if (!(*value_).HasMember(name.c_str())) {
      return default_value;
    } else {
      return getInteger(name);
    }
  }

  AbstractObjectPtr getObject(const std::string& name, bool allow_empty) const override {
    const rapidjson::Value& value = (*value_.get())[name.c_str()];
    if (allow_empty && !value.IsObject()) {
      rapidjson::Value empty_value;
      return AbstractObjectPtr{new AbstractObjectImpl(name)};
    } else if (!value.IsObject()) {
      throw Exception(fmt::format("key '{}' missing in '{}'", name, name_));
    }
    //  return AbstractObjectPtr{new AbstractObjectImpl(value, name)};
    return AbstractObjectPtr{new AbstractObjectImpl(name)};
  }

  std::vector<AbstractObjectPtr> getObjectArray(const std::string& name) const override {
    const rapidjson::Value& value = (*value_.get())[name.c_str()];
    if (!value.IsArray()) {
      throw Exception(fmt::format("key '{}' missing or not an array in '{}'", name, name_));
    }

    std::vector<AbstractObjectPtr> object_array;
    for (rapidjson::SizeType i = 0; i < value.Size(); i++) {
      //  object_array.emplace_back(new AbstractObjectImpl(value[i], name + " (array item)"));
      object_array.emplace_back(new AbstractObjectImpl(name + " (array item)"));
    }

    return object_array;
  }

  std::string getString(const std::string& name) const override {
    const rapidjson::Value& value = (*value_.get())[name.c_str()];
    if (!value.IsString()) {
      throw Exception(fmt::format("key '{}' missing or not a string in '{}'", name, name_));
    }

    return value.GetString();
  }

  std::string getString(const std::string& name, const std::string& default_value) const override {
    if (!value_.get()->HasMember(name.c_str())) {
      return default_value;
    } else {
      return getString(name);
    }
  }

  std::vector<std::string> getStringArray(const std::string& name) const override {
    const rapidjson::Value& value = (*value_.get())[name.c_str()];
    if (!value.IsArray()) {
      throw Exception(fmt::format("key '{}' missing or not an array in '{}'", name, name_));
    }

    std::vector<std::string> string_array;
    for (rapidjson::SizeType i = 0; i < value.Size(); i++) {
      if (!value[i].IsString()) {
        throw Exception(fmt::format("array '{}' does not contain all strings", name));
      }
      string_array.push_back(value[i].GetString());
    }
    return string_array;
  }

  double getDouble(const std::string& name) const override {
    const rapidjson::Value& value = (*value_.get())[name.c_str()];
    if (!value.IsDouble()) {
      throw Exception(fmt::format("key '{}' missing or not a double in '{}'", name, name_));
    }

    return value.GetDouble();
  }

  double getDouble(const std::string& name, double default_value) const override {
    if (!value_->HasMember(name.c_str())) {
      return default_value;
    } else {
      return getDouble(name);
    }
  }

  void iterate(const AbstractObjectCallback&) override {
    // temp commented out
    //    for (rapidjson::Value::ConstMemberIterator itr = document_.MemberBegin();
    //         itr != document_.MemberEnd(); ++itr) {
    //      std::string object_key(itr->name.GetString());
    //      AbstractObjectImpl object_value(itr->value, object_key);
    //      bool need_continue = callback(object_key, object_value);
    //      if (!need_continue) {
    //        break;
    //      }
    //    }
  }
  bool hasObject(const std::string& name) const override { return value_->HasMember(name.c_str()); }

  std::vector<AbstractObjectPtr> getMembers() const override {
    std::vector<AbstractObjectPtr> return_vector;
    for (rapidjson::Value::ConstMemberIterator itr = value_->MemberBegin();
         itr != value_->MemberEnd(); ++itr) {
      //  return_vnector.emplace_back(new AbstractObjectImpl(std::makeitr->value,
      //  itr->name.GetString()));
      return_vector.emplace_back(new AbstractObjectImpl(itr->name.GetString()));
    }
    return return_vector;
  }

  const std::string getName() const override { return name_; }

private:
  const std::string name_;
  std::shared_ptr<rapidjson::Value> value_;
};

AbstractObjectPtr Factory::LoadFromFile(const std::string& file_path) {
  rapidjson::Document* document = new rapidjson::Document();
  std::fstream file_stream(file_path);
  rapidjson::IStreamWrapper stream_wrapper(file_stream);
  if (document->ParseStream(stream_wrapper).HasParseError()) {
    throw Exception(fmt::format("Error(offset {}): {}\n", document->GetErrorOffset(),
                                GetParseError_En(document->GetParseError())));
  }
  std::shared_ptr<rapidjson::Value> value(document);
  return AbstractObjectPtr{new AbstractObjectImpl(value, "root")};
}

AbstractObjectPtr Factory::LoadFromString(const std::string& json) {
  rapidjson::Document* document = new rapidjson::Document();
  if (document->Parse<0>(json.c_str()).HasParseError()) {
    throw Exception(fmt::format("Error(offset {}): {}\n", document->GetErrorOffset(),
                                GetParseError_En(document->GetParseError())));
  }
  std::shared_ptr<rapidjson::Value> value(document);
  return AbstractObjectPtr{new AbstractObjectImpl(value, "root")};
}

} // Json
