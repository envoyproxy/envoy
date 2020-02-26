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
 * FilterState represents dynamically generated information regarding a stream (TCP or HTTP level)
 * or a connection by various filters in Envoy. FilterState can be write-once or write-many.
 */
class FilterState {
public:
  enum class StateType { ReadOnly, Mutable };
  // Objects stored in the FilterState may have different life span. Life span is what controls
  // how long an object stored in FilterState lives. Implementation of this interface actually
  // stores objects in a (reverse) tree manner - multiple FilterStateImpl with shorter life span may
  // share the same FilterStateImpl as parent, which may recursively share parent with other
  // FilterStateImpl at the same life span. This interface is supposed to be accessed at the leaf
  // level (FilterChain) for objects with any desired longer life span.
  //
  // - FilterChain has the shortest life span, which is as long as the filter chain lives.
  //
  // - DownstreamRequest is longer than FilterChain. When internal redirect is enabled, one
  //   downstream request may create multiple filter chains. DownstreamRequest allows an object to
  //   survive across filter chains for bookkeeping needs.
  //
  // - DownstreamConnection makes an object survive the entire duration of a downstream connection.
  //   Any stream within this connection can see the same object.
  //
  // Note that order matters in this enum because it's assumed that life span grows as enum number
  // grows.
  enum LifeSpan {
    FilterChain,
    DownstreamRequest,
    DownstreamConnection,
    TopSpan = DownstreamConnection
  };

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
   * @param life_span indicates the life span of the object: bound to the filter chain, a
   * downstream request, or a downstream connection.
   *
   * Note that it is an error to call setData() twice with the same
   * data_name, if the existing object is immutable. Similarly, it is an
   * error to call setData() with same data_name but different state_types
   * (mutable and readOnly, or readOnly and mutable) or different life_span.
   * This is to enforce a single authoritative source for each piece of
   * data stored in FilterState.
   */
  virtual void setData(absl::string_view data_name, std::shared_ptr<Object> data,
                       StateType state_type, LifeSpan life_span = LifeSpan::FilterChain) PURE;

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

  /**
   * @param life_span the LifeSpan above which data existence is checked.
   * @return whether data of any type exist with LifeSpan greater than life_span.
   */
  virtual bool hasDataAtOrAboveLifeSpan(LifeSpan life_span) const PURE;

  /**
   * @return the LifeSpan of objects stored by this instance. Objects with
   * LifeSpan longer than this are handled recursively.
   */
  virtual LifeSpan lifeSpan() const PURE;

  /**
   * @return the pointer of the parent FilterState that has longer life span. nullptr means this is
   * either the top LifeSpan or the parent is not yet created.
   */
  virtual std::shared_ptr<FilterState> parent() const PURE;

protected:
  virtual const Object* getDataReadOnlyGeneric(absl::string_view data_name) const PURE;
  virtual Object* getDataMutableGeneric(absl::string_view data_name) PURE;
};

using FilterStateSharedPtr = std::shared_ptr<FilterState>;

} // namespace StreamInfo
} // namespace Envoy
