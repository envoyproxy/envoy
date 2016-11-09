#include "json_loader.h"

// Do not let jansson leak outside of this file.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
//#include "jansson.h"
#include <rapidjson/error/en.h>
#include <rapidjson/filereadstream.h>
#pragma GCC diagnostic pop

namespace Json {

FileLoader::FileLoader(const std::string& file_path) {
  char buffer[4096];

  FILE* fp = fopen(file_path.c_str(), "r");
  if (!fp) {
    throw Exception("file doesn't exist");
  }

  rapidjson::FileReadStream fs(fp, buffer, sizeof(buffer));
  document_.ParseStream(fs);

  if (document_.HasParseError()) {
    throw Exception(fmt::format("Error(offset {}): {}\n",
                                static_cast<unsigned>(document_.GetErrorOffset()),
                                GetParseError_En(document_.GetParseError())));
  }
}

FileLoader::~FileLoader() {}

StringLoader::StringLoader(const std::string& json) {
  if (document_.Parse<0>(json.c_str()).HasParseError()) {
    throw Exception(fmt::format("Error(offset {}): {}\n",
                                static_cast<unsigned>(document_.GetErrorOffset()),
                                GetParseError_En(document_.GetParseError())));
  }
}

StringLoader::~StringLoader() {}

Object::EmptyObject Object::empty_;

Object::EmptyObject::EmptyObject() : document_() {}

Object::EmptyObject::~EmptyObject() {}

std::vector<Object> Object::asObjectArray() const {
  if (!document_.IsArray()) {
    throw Exception(fmt::format("'{}' is not an array", name_));
  }

  std::vector<Object> object_array;
  for (rapidjson::SizeType i = 0; i < document_.Size(); i++) {
    object_array.emplace_back(document_[i], name_ + " (array item)");
  }

  return object_array;
}

bool Object::getBoolean(const std::string& name) const {
  if (!document_.HasMember(name.c_str())) {
    throw Exception(fmt::format("key {} is missing", name));
  }
  if (!document_[name.c_str()].IsBool()) {
    throw Exception(fmt::format("Value for {} is not a boolean", name));
  }
  return document_[name.c_str()].GetBool();
}

bool Object::getBoolean(const std::string& name, bool default_value) const {
  if (!document_.HasMember(name.c_str())) {
    return default_value;
  } else {
    return getBoolean(name);
  }
}

int64_t Object::getInteger(const std::string& name) const {
  if (!document_.HasMember(name.c_str())) {
    throw Exception(fmt::format("key {} is missing", name));
  }
  if (!document_[name.c_str()].IsInt64()) {
    throw Exception(fmt::format("Value for {} is not an integer", name));
  }
  return document_[name.c_str()].GetInt64();
}

int64_t Object::getInteger(const std::string& name, int64_t default_value) const {
  if (!document_.HasMember(name.c_str())) {
    return default_value;
  } else {
    return getInteger(name);
  }
}

Object Object::getObject(const std::string& name, bool allow_empty) const {
  /*
  if (!object) {
    throw Exception(fmt::format("key '{}' missing in '{}'", name, name_));
  }
  if ( (!document_.HasMember(name.c_str()) || !document_[name.c_str()].IsObject()) && allow_empty){
    throw Exception(fmt::format("key '{}' missing in '{}'", name, name_));
  }
 */
  // return Object(object, name);
  if (allow_empty) {
    if (!document_.HasMember(name.c_str()) || !document_[name.c_str()].IsObject()) {
      return Object(name);
    }
  } else if (!document_.HasMember(name.c_str()) || !document_[name.c_str()].IsObject()) {
    throw Exception(fmt::format("key '{}' missing in '{}'", name, name_));
  }
  return Object(document_[name.c_str()], name);
}

std::vector<Object> Object::getObjectArray(const std::string& name) const {
  if (!document_.HasMember(name.c_str()) || !document_[name.c_str()].IsArray()) {
    throw Exception(fmt::format("key '{}' missing or not an array in '{}'", name, name_));
  }

  std::vector<Object> object_array;
  for (rapidjson::SizeType i = 0; i < document_[name.c_str()].Size(); i++) {
    object_array.emplace_back(document_[name.c_str()][i], name + " (array item)");
  }

  return object_array;
}

std::string Object::getString(const std::string& name) const {
  if (!document_.HasMember(name.c_str())) {
    throw Exception(fmt::format("key {} is missing", name));
  }
  if (!document_[name.c_str()].IsString()) {
    throw Exception(fmt::format("Value for {} is not a string", name));
  }

  return document_[name.c_str()].GetString();
}

std::string Object::getString(const std::string& name, const std::string& default_value) const {
  if (!document_.HasMember(name.c_str())) {
    return default_value;
  } else {
    return getString(name);
  }
}

std::vector<std::string> Object::getStringArray(const std::string& name) const {
  std::vector<std::string> string_array;
  for (Object& object : getObjectArray(name)) {
    if (!object.document_.IsString()) {
      throw Exception(fmt::format("array '{}' does not contain all strings", name));
    }
    string_array.push_back(object.document_.GetString());
  }

  return string_array;
}

double Object::getDouble(const std::string& name) const {
  if (!document_.HasMember(name.c_str()) || !document_[name.c_str()].IsDouble()) {

    throw Exception(fmt::format("key '{}' missing or not a double in '{}'", name, name_));
  }

  return document_[name.c_str()].GetDouble();
}

double Object::getDouble(const std::string& name, double default_value) const {
  if (!document_.HasMember(name.c_str())) {
    return default_value;
  } else {
    return getDouble(name);
  }
}

void Object::iterate(const ObjectCallback& callback) {

  for (rapidjson::Value::ConstMemberIterator itr = document_.MemberBegin();
       itr != document_.MemberEnd(); ++itr) {
    std::string object_key(itr->name.GetString());
    Object object_value(itr->value, "root");
    bool need_continue = callback(object_key, object_value);
    if (!need_continue) {
      break;
    }
  }
}

bool Object::hasObject(const std::string& name) const {
  return document_.HasMember(name.c_str()) && document_.IsObject();
}

} // Json
