#pragma once

#include "envoy/common/exception.h"

#include "common/common/non_copyable.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include "rapidjson/document.h"
#pragma GCC diagnostic pop

namespace Json {

/**
 * Exception thrown when a JSON error occurs.
 */
class Exception : public EnvoyException {
public:
  Exception(const std::string& message) : EnvoyException(message) {}
};

class Object;
// @return false if immediate exit from iteration required.
typedef std::function<bool(const std::string&, const Object&)> ObjectCallback;

/**
 * Wraps an individual JSON node. Nodes are only valid while the Loader root object is alive.
 */
class Object {
public:
  Object(const rapidjson::Value& value, const std::string& name) : name_(name), document_() {
    document_.CopyFrom(value, document_.GetAllocator());
  }

  // Empty Object
  Object(const std::string& name) : name_(name), document_() {}

  /**
   * Convert a generic object into an array of objects. This is useful for dealing with arrays
   * of arrays.
   * @return std::vector<Object> the converted object.
   */
  std::vector<Object> asObjectArray() const;

  /**
   * Get a boolean value by name.
   * @param name supplies the key name.
   * @return bool the value.
   */
  bool getBoolean(const std::string& name) const;

  /**
   * Get a boolean value by name.
   * @param name supplies the  key name.
   * @param default_value supplies the value to return if the name does not exist.
   * @return bool the value.
   */
  bool getBoolean(const std::string& name, bool default_value) const;

  /**
   * Get an integer value by name.
   * @param name supplies the key name.
   * @return int64_t the value.
   */
  int64_t getInteger(const std::string& name) const;

  /**
   * Get an integer value by name or return a default if name does not exist.
   * @param name supplies the key name.
   * @param default_value supplies the value to return if name does not exist.
   * @return int64_t the value.
   */
  int64_t getInteger(const std::string& name, int64_t default_value) const;

  /**
   * Get a sub-object by name.
   * @param name supplies the key name.
   * @param allow_empty supplies whether to return an empty object if the key does not exist.
   * @return Object the sub-object.
   */
  Object getObject(const std::string& name, bool allow_empty = false) const;

  /**
   * Get an array by name.
   * @param name supplies the key name.
   * @return std::vector<Object> the array of JSON objects.
   */
  std::vector<Object> getObjectArray(const std::string& name) const;

  /**
   * Get a string value by name.
   * @param name supplies the key name.
   * @return std::string the value.
   */
  std::string getString(const std::string& name) const;

  /**
   * Get a string value by name or return a default if name does not exist.
   * @param name supplies the key name.
   * @param default_value supplies the value to return if name does not exist.
   * @return std::string the value.
   */
  std::string getString(const std::string& name, const std::string& default_value) const;

  /**
   * Get a string array by name.
   * @param name supplies the key name.
   * @return std::vector<std::string> the array of strings.
   */
  std::vector<std::string> getStringArray(const std::string& name) const;

  /**
   * Get a double value by name.
   * @param name supplies the key name.
   * @return double the value.
   */
  double getDouble(const std::string& name) const;

  /**
   * Get a double value by name.
   * @param name supplies the key name.
   * @param default_value supplies the value to return if name does not exist.
   * @return double the value.
   */
  double getDouble(const std::string& name, double default_value) const;

  /**
   * Iterate Object and call callback on key-value pairs
   */
  void iterate(const ObjectCallback& callback);

  /**
   * @return TRUE if the object contains an element.
   * @param name supplies the key name to lookup.
   */
  bool hasObject(const std::string& name) const;

protected:
  Object() : name_("root"), document_() {}

  std::string name_;
  rapidjson::Document document_;
};

/**
 * Loads a JSON file into memory.
 */
class FileLoader : NonCopyable, public Object {
public:
  FileLoader(const std::string& file_path);
  ~FileLoader();
};

/**
 * Loads JSON from a string.
 */
class StringLoader : NonCopyable, public Object {
public:
  StringLoader(const std::string& json);
  ~StringLoader();
};

} // Json
