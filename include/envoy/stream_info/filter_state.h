#pragma once

#include <functional>
#include <memory>

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
   * The addToList, hasList, forEachListItem operate on list data. Multiple
   * elements could be added the list under the same list_name.
   * @param list_name the name of the list data being set.
   * @param item an owning pointer to the item to be appended to the list.
   * Note that names for list elements do not share the same namespace as
   * the names for singleton data objects added through setData. All items
   * added to the list must be of the same type.
   */
  template <typename T>
  void addToList(absl::string_view list_name, std::unique_ptr<Object>&& item) {
    const auto* list = getList(list_name);
    if (list != nullptr) {
      // Check type of first element in the list
      const T* cast = dynamic_cast<const T*>(list->at(0).get());
      if (!cast) {
        throw EnvoyException(
            fmt::format("List {} does not conform to the specified type", list_name));
      }
    }

    addToListGeneric(list_name, std::move(item));
  }

  /**
   * @param list_name the name of the list data being looked up.
   * @param operation a lambda function that operates on each element in the list,
   * if it exists. The iteration will stop if the lambda function returns false or
   * reaches the end of the list. The iteration order will be the same as the order
   * in which the elements were added to the list. Note that if the elements in the
   * list cannot be dynamically type-cast into the requested type, an exception
   * will be thrown.
   */
  template <typename T>
  void forEachListItem(absl::string_view list_name, std::function<bool(const T&)> op) const {
    const auto* list = getList(list_name);
    if (!list) {
      return;
    }

    for (const auto& it : *list) {
      const T* item = dynamic_cast<const T*>(it.get());
      if (!item) {
        throw EnvoyException(
            fmt::format("Element in list {} cannot be coerced to specified type", list_name));
      }
      if (!op(*item)) {
        break;
      }
    }
  }

  /**
   * @param data_name the name of the data being probed.
   * @return Whether data of any type and the name specified exists in the
   * data store.
   */
  virtual bool hasData(absl::string_view data_name) const PURE;

  /**
   * @param list_name the name of the list being probed.
   * @return Whether a list of any type and the name specified exists in the
   * data store.
   */
  virtual bool hasList(absl::string_view list_name) const PURE;

protected:
  virtual const Object* getDataGeneric(absl::string_view data_name) const PURE;
  virtual const std::vector<std::unique_ptr<Object>>*
  getList(absl::string_view list_name) const PURE;
  virtual void addToListGeneric(absl::string_view list_name, std::unique_ptr<Object>&& item) PURE;
};

} // namespace StreamInfo
} // namespace Envoy
