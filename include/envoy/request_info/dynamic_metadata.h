#pragma once

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/common/fmt.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RequestInfo {

class DynamicMetadata {
public:
  class Object {
  public:
    virtual ~Object(){};
  };

  virtual ~DynamicMetadata(){};

  /**
   * @param data_name the name of the data being set.
   * @param data an owning pointer to the data to be stored.
   * Note that it is an error to call setData() twice with the same data_name; this is to
   * enforce a single authoritative source for each piece of data stored in DynamicMetadata.
   */
  virtual void setData(absl::string_view data_name, std::unique_ptr<Object>&& data) PURE;

  /**
   * @param data_name the name of the data being set.
   * @return a reference to the stored data.
   * Note that it is an error to access data that has not previously been set.
   * This function will fail if the data stored under |data_name| cannot be
   * dynamically cast to the type specified.
   */
  template <typename T> const T& getData(absl::string_view data_name) const {
    const T* result = dynamic_cast<const T*>(getDataGeneric(data_name));
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
            (dynamic_cast<const T*>(getDataGeneric(data_name)) != nullptr));
  }

  /**
   * @param data_name the name of the data being probed.
   * @return Whether data of any type and the name specified exists in the
   * data store.
   */
  virtual bool hasDataWithName(absl::string_view data_name) const PURE;

protected:
  virtual const Object* getDataGeneric(absl::string_view data_name) const PURE;
};

} // namespace RequestInfo
} // namespace Envoy
