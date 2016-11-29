#pragma once

#include "envoy/common/pure.h"

namespace Json {

class AbstractObject;

typedef std::unique_ptr<AbstractObject> AbstractObjectPtr;

// @return false if immediate exit from iteration required.
typedef std::function<bool(const std::string&, const AbstractObject&)> AbstractObjectCallback;

/**
 * Wraps an individual JSON node.
 */
class AbstractObject {
public:
  virtual ~AbstractObject() {}

  /**
   * Convert a generic abstract object into an array of abstract objects. This is useful for dealing
   * with arrays of arrays.
   * @return std::vector<AbstractObjectPtr> the converted abstract object.
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
   * @param name supplies the key name.
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
   * @param allow_empty supplies whether to return an empty abstract object if the key does not
   * exist.
   * @return AbstractObjectPtr the sub-object.
   */
  virtual AbstractObjectPtr getObject(const std::string& name, bool allow_empty = false) const PURE;

  /**
   * Get an array by name.
   * @param name supplies the key name.
   * @return std::vector<AbstractObjectPtr> the array of JSON abstract objects.
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
   * Iterate over key-value pairs in an AbstractObject and call callback on each pair.
   */
  virtual void iterate(const AbstractObjectCallback& callback) const PURE;

  /**
   * @return TRUE if the AbstractObject contains the key.
   * @param name supplies the key name to lookup.
   */
  virtual bool hasObject(const std::string& name) const PURE;
};

} // Json
