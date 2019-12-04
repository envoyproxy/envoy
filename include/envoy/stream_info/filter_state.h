#pragma once

#include <memory>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/common/fmt.h"
#include "common/protobuf/protobuf.h"

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
  // When internal redirect is enabled, one downstream request may create multiple filter chains.
  // DownstreamRequest allows an object to survived across filter chain for book keeping.
  // Note that order matters in this enum because it's assumed that life span grows as enum number
  // grows.
  enum class LifeSpan { FilterChain, DownstreamRequest };

  class Object {
  public:
    virtual ~Object() = default;

    /**
     * @return Protobuf::MessagePtr an unique pointer to the proto serialization of the filter
     * state. If returned message type is ProtobufWkt::Any it will be directly used in protobuf
     * logging. nullptr if the filter state cannot be serialized or serialization is not supported.
     */
    virtual ProtobufTypes::MessagePtr serializeAsProto() const { return nullptr; }
  };

  virtual ~FilterState() = default;

  /**
   * @param data_name the name of the data being set.
   * @param data an owning pointer to the data to be stored.
   * @param state_type indicates whether the object is mutable or not.
   * @param life_span indicates the life span of the object: bound to the filter chain or a
   * downstream request.
   *
   * Note that it is an error to call setData() twice with the same
   * data_name, if the existing object is immutable. Similarly, it is an
   * error to call setData() with same data_name but different state_types
   * (mutable and readOnly, or readOnly and mutable). This is to enforce a
   * single authoritative source for each piece of immutable data stored in
   * FilterState.
   */
  virtual void setData(absl::string_view data_name, std::shared_ptr<Object> data,
                       StateType state_type, LifeSpan life_span) PURE;

  /**
   * @param other the FilterState we want to copy current data into.
   * @param life_span the object life span above or equal to which will be copied.
   *
   * This is useful for sharing filter state within bigger life span than a filter chain. For
   * example, we are going to use it to share router filter state across filter chains created on
   * the same downstream request for internal redirect handling.
   *
   * This can be extended to support connection level sharing fairly easily, but there is not a need
   * yet.
   */
  virtual void copyInto(FilterState& other, LifeSpan life_span) PURE;

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
