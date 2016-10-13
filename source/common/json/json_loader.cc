#include "json_loader.h"

// Do not let jansson leak outside of this file.
#include "jansson.h"

namespace Json {

FileLoader::FileLoader(const std::string& file_path) {
  json_error_t error;
  json_ = json_load_file(file_path.c_str(), 0, &error);
  if (!json_) {
    throw Exception(fmt::format("json parsing error: {} line: {} column: {}", error.text,
                                error.line, error.column));
  }
}

FileLoader::~FileLoader() { json_decref(json_); }

StringLoader::StringLoader(const std::string& json) {
  json_error_t error;
  json_ = json_loads(json.c_str(), 0, &error);
  if (!json_) {
    throw Exception(fmt::format("json parsing error: {}", error.text));
  }
}

StringLoader::~StringLoader() { json_decref(json_); }

Object::EmptyObject Object::empty_;

Object::EmptyObject::EmptyObject() : json_(json_object()) {}

Object::EmptyObject::~EmptyObject() { json_decref(json_); }

std::vector<Object> Object::asObjectArray() const {
  if (!json_is_array(json_)) {
    throw Exception(fmt::format("'{}' is not an array", name_));
  }

  std::vector<Object> object_array;
  for (size_t i = 0; i < json_array_size(json_); i++) {
    object_array.emplace_back(json_array_get(json_, i), name_ + " (array item)");
  }

  return object_array;
}

bool Object::getBoolean(const std::string& name) const {
  json_t* boolean = json_object_get(json_, name.c_str());
  if (!boolean || !json_is_boolean(boolean)) {
    throw Exception(fmt::format("key '{}' missing or not a boolean in '{}'", name, name_));
  }

  return json_boolean_value(boolean);
}

bool Object::getBoolean(const std::string& name, bool default_value) const {
  if (!json_object_get(json_, name.c_str())) {
    return default_value;
  } else {
    return getBoolean(name);
  }
}

int64_t Object::getInteger(const std::string& name) const {
  json_t* integer = json_object_get(json_, name.c_str());
  if (!integer || !json_is_integer(integer)) {
    throw Exception(fmt::format("key '{}' missing or not an integer in '{}'", name, name_));
  }

  return json_integer_value(integer);
}

int64_t Object::getInteger(const std::string& name, int64_t default_value) const {
  if (!json_object_get(json_, name.c_str())) {
    return default_value;
  } else {
    return getInteger(name);
  }
}

Object Object::getObject(const std::string& name, bool allow_empty) const {
  json_t* object = json_object_get(json_, name.c_str());
  if (!object && allow_empty) {
    object = empty_.json_;
  }

  if (!object) {
    throw Exception(fmt::format("key '{}' missing in '{}'", name, name_));
  }

  return Object(object, name);
}

std::vector<Object> Object::getObjectArray(const std::string& name) const {
  json_t* array = json_object_get(json_, name.c_str());
  if (!array || !json_is_array(array)) {
    throw Exception(fmt::format("key '{}' missing or not an array in '{}'", name, name_));
  }

  std::vector<Object> object_array;
  for (size_t i = 0; i < json_array_size(array); i++) {
    object_array.emplace_back(json_array_get(array, i), name + " (array item)");
  }

  return object_array;
}

std::string Object::getString(const std::string& name) const {
  json_t* string = json_object_get(json_, name.c_str());
  if (!string || !json_is_string(string)) {
    throw Exception(fmt::format("key '{}' missing or not a string in '{}'", name, name_));
  }

  return json_string_value(string);
}

std::string Object::getString(const std::string& name, const std::string& default_value) const {
  if (!json_object_get(json_, name.c_str())) {
    return default_value;
  } else {
    return getString(name);
  }
}

std::vector<std::string> Object::getStringArray(const std::string& name) const {
  std::vector<std::string> string_array;
  for (Object& object : getObjectArray(name)) {
    if (!json_is_string(object.json_)) {
      throw Exception(fmt::format("array '{}' does not contain all strings", name));
    } else {
      string_array.push_back(json_string_value(object.json_));
    }
  }

  return string_array;
}

double Object::getDouble(const std::string& name) const {
  json_t* real = json_object_get(json_, name.c_str());
  if (!real || !json_is_real(real)) {
    throw Exception(fmt::format("key '{}' missing or not a double in '{}'", name, name_));
  }

  return json_real_value(real);
}

double Object::getDouble(const std::string& name, double default_value) const {
  if (!json_object_get(json_, name.c_str())) {
    return default_value;
  } else {
    return getDouble(name);
  }
}

void Object::iterate(const ObjectCallback& callback) {
  const char* key;
  json_t* value;

  json_object_foreach(json_, key, value) {
    Object object_value(value, "root");
    std::string object_key(key);

    bool need_continue = callback(object_key, object_value);
    if (!need_continue) {
      break;
    }
  }
}

bool Object::hasObject(const std::string& name) const {
  return json_object_get(json_, name.c_str()) != nullptr;
}

} // Json
