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
  Object() {}

  Object(AbstractObjectPtr&& abstract_object_ptr) {
    abstract_object_ = std::move(abstract_object_ptr);
  }

  std::vector<Object> asObjectArray() const {
    std::vector<AbstractObjectPtr> abstract_object_array = abstract_object_->asObjectArray();
    std::vector<Object> return_vector;
    for (AbstractObjectPtr& abstract_object : abstract_object_array) {
      return_vector.emplace_back(Object(std::move(abstract_object)));
    }
    return return_vector;
  }

  bool getBoolean(const std::string& name) const { return abstract_object_->getBoolean(name); }

  bool getBoolean(const std::string& name, bool default_value) const {
    return abstract_object_->getBoolean(name, default_value);
  }

  int64_t getInteger(const std::string& name) const { return abstract_object_->getInteger(name); }

  int64_t getInteger(const std::string& name, int64_t default_value) const {
    return abstract_object_->getInteger(name, default_value);
  }

  Object getObject(const std::string& name, bool allow_empty = false) const {
    AbstractObjectPtr abstract_object = abstract_object_->getObject(name, allow_empty);
    return Object(std::move(abstract_object));
  }

  std::vector<Object> getObjectArray(const std::string& name) const {
    std::vector<AbstractObjectPtr> abstract_object_array = abstract_object_->getObjectArray(name);
    std::vector<Object> return_vector;
    for (AbstractObjectPtr& abstract_object : abstract_object_array) {
      return_vector.emplace_back(Object(std::move(abstract_object)));
    }
    return return_vector;
  }

  std::string getString(const std::string& name) const { return abstract_object_->getString(name); }

  std::string getString(const std::string& name, const std::string& default_value) const {
    return abstract_object_->getString(name, default_value);
  }

  std::vector<std::string> getStringArray(const std::string& name) const {
    return abstract_object_->getStringArray(name);
  }

  double getDouble(const std::string& name) const { return abstract_object_->getDouble(name); }

  double getDouble(const std::string& name, double default_value) const {
    return abstract_object_->getDouble(name, default_value);
  }

  void iterate(const ObjectCallback& callback) {
    for (AbstractObjectPtr& object : abstract_object_->getMembers()) {
      std::string object_key(object->getName());
      Object json(std::move(object));
      bool need_continue = callback(object_key, json);
      if (!need_continue) {
        break;
      }
    }
  }

  bool hasObject(const std::string& name) const { return abstract_object_->hasObject(name); }

protected:
  AbstractObjectPtr abstract_object_;
};

/**
 * Loads a JSON file into memory.
 */
class FileLoader : NonCopyable, public Object {
public:
  FileLoader(const std::string& file_path) { abstract_object_ = Factory::LoadFromFile(file_path); }
};

/**
 * Loads JSON from a string.
 */
class StringLoader : NonCopyable, public Object {
public:
  StringLoader(const std::string& json) { abstract_object_ = Factory::LoadFromString(json); }
};

} // Json
