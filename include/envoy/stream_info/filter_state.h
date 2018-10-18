#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/common/fmt.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace StreamInfo {

class FilterState {
public:
  class Object {
  public:
    virtual ~Object(){};
  };

  virtual ~FilterState(){};

  /**
   * @param data_name the name of the data being set.
   * @param data an owning pointer to the data to be stored.
   * Note that it is an error to call setData() twice with the same data_name; this is to
   * enforce a single authoritative source for each piece of data stored in FilterState.
   */
  virtual void setData(absl::string_view data_name, std::unique_ptr<Object>&& data) PURE;

  /**
   * @param data_name the name of the data being looked up.
   * @return a const reference to the stored data.
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
   * The addToList, hasListWithName, forEachListItem are variants of the above
   * functions, that operate on list data. Multiple elements could be added
   * to the list under the same data_name.
   * @param data_name the name of the list data being set.
   * @param data an owning pointer to the data to be appended to the list.
   * Note that data_names for list elements do not share the same namespace as
   * the data_names for singleton data objects added through setData. All items
   * added to the list must be of the same type.
   */
  template <typename T>
  void addToList(absl::string_view data_name, std::unique_ptr<Object>&& data) {
    const auto* list = getList(data_name);
    if (list != nullptr) {
      // Check type of first element in the list
      const T* cast = dynamic_cast<const T*>(list->at(0).get());
      if (!cast) {
        throw EnvoyException(
            fmt::format("List {} does not conform to the specified type", data_name));
      }
    }

    addToListGeneric(data_name, std::move(data));
  }

  /**
   * @param data_name the name of the list being probed.
   * @return Whether a list of the type and name specified exists in the
   * data store.
   */
  template <typename T> bool hasList(absl::string_view data_name) const {
    const auto* list = getList(data_name);
    return ((list != nullptr) && (dynamic_cast<const T*>(list->at(0).get()) != nullptr));
  }

  /**
   * @param data_name the name of the list data being looked up.
   * @param operation a lambda function that operates on each element in the list,
   * if it exists. The iteration will stop if the lambda function returns false or
   * reaches the end of the list. The iteration order will be the same as the order
   * in which the elements were added to the list. Note that if the elements in the
   * list cannot be dynamically type-cast into the requested type, an exception
   * will be thrown.
   */
  template <typename T>
  void forEachListItem(absl::string_view data_name, std::function<bool(const T&)> op) const {
    const auto* list = getList(data_name);
    if (!list) {
      return;
    }

    for (const auto& it : *list) {
      const T* data = dynamic_cast<const T*>(it.get());
      if (!data) {
        throw EnvoyException(
            fmt::format("Element in list {} cannot be coerced to specified type", data_name));
      }
      if (!op(*data)) {
        break;
      }
    }
  }

  /**
   * @param data_name the name of the data being probed.
   * @return Whether data of any type and the name specified exists in the
   * data store.
   */
  virtual bool hasDataWithName(absl::string_view data_name) const PURE;

  /**
   * @param data_name the name of the list data being probed.
   * @return Whether a list of any type and the name specified exists in the
   * data store.
   */
  virtual bool hasListWithName(absl::string_view data_name) const PURE;

protected:
  virtual const Object* getDataGeneric(absl::string_view data_name) const PURE;
  virtual const std::vector<std::unique_ptr<Object>>*
  getList(absl::string_view data_name) const PURE;
  virtual void addToListGeneric(absl::string_view data_name, std::unique_ptr<Object>&& data) PURE;
};

} // namespace StreamInfo
} // namespace Envoy
