#pragma once

#include "envoy/common/exception.h"
#include "envoy/json/json_object.h"

#include "common/common/non_copyable.h"

namespace Json {

class Factory {
public:
  /*
   * Constructs a Json Object from a File.
   */
  static AbstractObjectPtr LoadFromFile(const std::string& file_path);

  /*
   * Constructs a Json Object from a String.
   */
  static AbstractObjectPtr LoadFromString(const std::string& json);
};

/**
 * Exception thrown when a JSON error occurs.
 */
class Exception : public EnvoyException {
public:
  Exception(const std::string& message) : EnvoyException(message) {}
};

/**
 * Wrapper around AbstractObjectPtr to maintain calling pattern across code base.
 */
class Object;
// @return false if immediate exit from iteration required.
typedef std::function<bool(const std::string&, const Object&)> ObjectCallback;

class Object {
public:
  Object(const AbstractObject& abstract_object) : abstract_object_(abstract_object) {}

  std::vector<Object> asObjectArray() const; /*{
    std::vector<AbstractObjectPtr> abstract_object_array = abstract_object_.asObjectArray();
    std::vector<Object> return_vector;
    return_vector.reserve(abstract_object_array.size());

    for (AbstractObjectPtr& abstract_object : abstract_object_array) {
      return_vector.emplace_back(*abstract_object);
    }
    return return_vector;
  }*/

  bool getBoolean(const std::string& name) const { return abstract_object_.getBoolean(name); }

  bool getBoolean(const std::string& name, bool default_value) const {
    return abstract_object_.getBoolean(name, default_value);
  }

  int64_t getInteger(const std::string& name) const { return abstract_object_.getInteger(name); }

  int64_t getInteger(const std::string& name, int64_t default_value) const {
    return abstract_object_.getInteger(name, default_value);
  }

  Object getObject(const std::string& name, bool allow_empty = false) const; /* {
    return Object(*(abstract_object_.getObject(name, allow_empty)));
  }*/

  std::vector<Object> getObjectArray(const std::string& name) const; /* {
    std::vector<AbstractObjectPtr> abstract_object_array = abstract_object_.getObjectArray(name);
    std::vector<Object> return_vector;
    return_vector.reserve(abstract_object_array.size());

    for (AbstractObjectPtr& abstract_object : abstract_object_array) {
      return_vector.emplace_back(*abstract_object);
    }
    return return_vector;
  }*/

  std::string getString(const std::string& name) const { return abstract_object_.getString(name); }

  std::string getString(const std::string& name, const std::string& default_value) const {
    return abstract_object_.getString(name, default_value);
  }

  std::vector<std::string> getStringArray(const std::string& name) const {
    return abstract_object_.getStringArray(name);
  }

  double getDouble(const std::string& name) const { return abstract_object_.getDouble(name); }

  double getDouble(const std::string& name, double default_value) const {
    return abstract_object_.getDouble(name, default_value);
  }

  void iterate(const ObjectCallback& callback) {
    abstract_object_.iterate(
        [&callback](const std::string key, const AbstractObject& abstract_object) {
          Object object(abstract_object);
          return callback(key, object);
        });
  }

  bool hasObject(const std::string& name) const { return abstract_object_.hasObject(name); }

private:
  const AbstractObject& abstract_object_;
};
class ObjectRoot : public Object {
public:
  ObjectRoot(AbstractObjectPtr&& abstract_object_ptr)
      : Object(*abstract_object_ptr), abstract_object_ptr_(std::move(abstract_object_ptr)) {}

private:
  AbstractObjectPtr abstract_object_ptr_;
};
/**
 * Loads a JSON file into memory.
 */
class FileLoader : NonCopyable, public ObjectRoot {
public:
  FileLoader(const std::string& file_path)
      : ObjectRoot(std::move(Factory::LoadFromFile(file_path))) {}
};

/**
 * Loads JSON from a string.
 */
class StringLoader : NonCopyable, public ObjectRoot {
public:
  StringLoader(const std::string& json) : ObjectRoot(std::move(Factory::LoadFromString(json))) {}
};

} // Json
