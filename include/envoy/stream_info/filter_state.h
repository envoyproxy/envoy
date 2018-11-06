#pragma once

#include <memory>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/common/fmt.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace StreamInfo {

/**
 * FilterState represents dynamically generated information regarding a
 * stream (TCP or HTTP level) by various filters in Envoy. FilterState can
 * be write-once or write-many.
 */
class FilterState {
public:
  enum class StateType { ReadOnly, Mutable };

  class Object {
  public:
    virtual ~Object(){};
  };

  virtual ~FilterState(){};

  /**
   * @param data_name the name of the data being set.
   * @param data an owning pointer to the data to be stored.
   * @param state_type indicates whether the object is mutable or not.
   *
   * Note that it is an error to call setData() twice with the same
   * data_name, if the existing object is immutable. Similarly, it is an
   * error to call setData() with same data_name but different state_types
   * (mutable and readOnly, or readOnly and mutable). This is to enforce a
   * single authoritative source for each piece of immutable data stored in
   * FilterState.
   */
  virtual void setData(absl::string_view data_name, std::unique_ptr<Object>&& data,
                       StateType state_type) PURE;

  /**
   * @param data_name the name of the data being looked up (mutable/readonly).
   * @return a const reference to the stored data.
   * An exception will be thrown if the data does not exist. This function
   * will fail if the data stored under |data_name| cannot be dynamically
   * cast to the type specified.
   */
  template <typename T> const T& getDataReadOnly(absl::string_view data_name) const {
    const T* result = dynamic_cast<const T*>(getDataReadOnlyGeneric(data_name));
    if (!result) {
      throw EnvoyException(
          fmt::format("Data stored under {} cannot be coerced to specified type", data_name));
    }
    return *result;
  }

  /**
   * @param data_name the name of the data being looked up (mutable only).
   * @return a non-const reference to the stored data if and only if the
   * underlying data is mutable.
   * An exception will be thrown if the data does not exist or if it is
   * immutable. This function will fail if the data stored under
   * |data_name| cannot be dynamically cast to the type specified.
   */
  template <typename T> T& getDataMutable(absl::string_view data_name) {
    T* result = dynamic_cast<T*>(getDataMutableGeneric(data_name));
    if (!result) {
      throw EnvoyException(
          fmt::format("Data stored under {} cannot be coerced to specified type", data_name));
    }
    return *result;
  }

  /**
   * @param data_name the name of the data being probed.
   * @return Whether data of the type and name specified exists in the
   * data store.
   */
  template <typename T> bool hasData(absl::string_view data_name) const {
    return (hasDataWithName(data_name) &&
            (dynamic_cast<const T*>(getDataReadOnlyGeneric(data_name)) != nullptr));
  }

  /**
   * @param data_name the name of the data being probed.
   * @return Whether data of any type and the name specified exists in the
   * data store.
   */
  virtual bool hasDataWithName(absl::string_view data_name) const PURE;

protected:
  virtual const Object* getDataReadOnlyGeneric(absl::string_view data_name) const PURE;
  virtual Object* getDataMutableGeneric(absl::string_view data_name) PURE;
};

} // namespace StreamInfo
} // namespace Envoy
