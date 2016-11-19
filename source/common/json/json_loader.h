#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/common/non_copyable.h"

namespace Json {

class AbstractObject;
// @return false if immediate exit from iteration required.
typedef std::function<bool(const std::string&, const AbstractObject&)> AbstractObjectCallback;

typedef std::unique_ptr<AbstractObject> AbstractObjectPtr;

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
 * Wraps an individual JSON node. Nodes are only valid while the Loader root object is alive.
 */
class AbstractObject {
public:
  virtual ~AbstractObject() {}

  /**
   * Convert a generic object into an array of objects. This is useful for dealing with arrays
   * of arrays.
   * @return std::vector<Object> the converted object.
   */
  virtual std::vector<AbstractObjectPtr> asObjectArray() const PURE;

  /**
   * Get a boolean value by name.
   * @param name supplies the key name.
   * @return bool the value.
   */
  virtual bool getBoolean(const std::string& name) const PURE;

  /**
   * Get a boolean value by name.
   * @param name supplies the  key name.
   * @param default_value supplies the value to return if the name does not exist.
   * @return bool the value.
   */
  virtual bool getBoolean(const std::string& name, bool default_value) const PURE;

  /**
   * Get an integer value by name.
   * @param name supplies the key name.
   * @return int64_t the value.
   */
  virtual int64_t getInteger(const std::string& name) const PURE;

  /**
   * Get an integer value by name or return a default if name does not exist.
   * @param name supplies the key name.
   * @param default_value supplies the value to return if name does not exist.
   * @return int64_t the value.
   */
  virtual int64_t getInteger(const std::string& name, int64_t default_value) const PURE;

  /**
   * Get a sub-object by name.
   * @param name supplies the key name.
   * @param allow_empty supplies whether to return an empty object if the key does not exist.
   * @return Object the sub-object.
   */
  virtual AbstractObjectPtr getObject(const std::string& name, bool allow_empty = false) const PURE;

  /**
   * Get an array by name.
   * @param name supplies the key name.
   * @return std::vector<Object> the array of JSON objects.
   */
  virtual std::vector<AbstractObjectPtr> getObjectArray(const std::string& name) const PURE;

  /**
   * Get a string value by name.
   * @param name supplies the key name.
   * @return std::string the value.
   */
  virtual std::string getString(const std::string& name) const PURE;

  /**
   * Get a string value by name or return a default if name does not exist.
   * @param name supplies the key name.
   * @param default_value supplies the value to return if name does not exist.
   * @return std::string the value.
   */
  virtual std::string getString(const std::string& name,
                                const std::string& default_value) const PURE;

  /**
   * Get a string array by name.
   * @param name supplies the key name.
   * @return std::vector<std::string> the array of strings.
   */
  virtual std::vector<std::string> getStringArray(const std::string& name) const PURE;

  /**
   * Get a double value by name.
   * @param name supplies the key name.
   * @return double the value.
   */
  virtual double getDouble(const std::string& name) const PURE;

  /**
   * Get a double value by name.
   * @param name supplies the key name.
   * @param default_value supplies the value to return if name does not exist.
   * @return double the value.
   */
  virtual double getDouble(const std::string& name, double default_value) const PURE;

  /**
   * Iterate Object and call callback on key-value pairs
   */
  virtual void iterate(const AbstractObjectCallback& callback) PURE;

  /**
   * @return TRUE if the object contains an element.
   * @param name supplies the key name to lookup.
   */
  virtual bool hasObject(const std::string& name) const PURE;

  virtual std::vector<AbstractObjectPtr> getMembers() const PURE;

  virtual const std::string getName() const PURE;
};

class Object;

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

class StringLoader : public Object {
public:
  StringLoader(const std::string& json) { abstract_object_ = Factory::LoadFromString(json); }
};

class FileLoader : public Object {
public:
  FileLoader(const std::string& file_path) { abstract_object_ = Factory::LoadFromFile(file_path); }
};

} // Json
