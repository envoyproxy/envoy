#pragma once

#include <list>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "source/common/common/utility.h"
#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class ThriftBase;

/**
 * ThriftValue is a field or container (list, set, or map) element.
 */
class ThriftValue {
public:
  virtual ~ThriftValue() = default;

  /**
   * @return FieldType the type of this value
   */
  virtual FieldType type() const PURE;

  /**
   * @return const T& pointer to the value, provided that it can be cast to the given type
   * @throw EnvoyException if the type T does not match the type
   */
  template <typename T> const T& getValueTyped() const {
    // Use the Traits template to determine what FieldType the value must have to be cast to T
    // and throw if the value's type doesn't match.
    FieldType expected_field_type = Traits<T>::getFieldType();
    if (expected_field_type != type()) {
      ExceptionUtil::throwEnvoyException(fmt::format("expected field type {}, got {}",
                                                     static_cast<int>(expected_field_type),
                                                     static_cast<int>(type())));
    }

    return *static_cast<const T*>(getValue());
  }

protected:
  /**
   * @return void* pointing to the underlying value, to be dynamically cast in getValueTyped
   */
  virtual const void* getValue() const PURE;

private:
  /**
   * Traits allows getValueTyped() to enforce that the field type is being cast to the desired type.
   */
  template <typename T> class Traits {
  public:
    // Compilation failures where T does not have a member getFieldType typically mean that
    // getValueTyped was called with a type T that is not used to encode Thrift values.
    // The specializations below encode the valid types for Thrift primitive types.
    static FieldType getFieldType() { return T::getFieldType(); }
  };
};

// Explicit specializations of ThriftValue::Types for primitive types.
template <> class ThriftValue::Traits<bool> {
public:
  static FieldType getFieldType() { return FieldType::Bool; }
};

template <> class ThriftValue::Traits<uint8_t> {
public:
  static FieldType getFieldType() { return FieldType::Byte; }
};

template <> class ThriftValue::Traits<int16_t> {
public:
  static FieldType getFieldType() { return FieldType::I16; }
};

template <> class ThriftValue::Traits<int32_t> {
public:
  static FieldType getFieldType() { return FieldType::I32; }
};

template <> class ThriftValue::Traits<int64_t> {
public:
  static FieldType getFieldType() { return FieldType::I64; }
};

template <> class ThriftValue::Traits<double> {
public:
  static FieldType getFieldType() { return FieldType::Double; }
};

template <> class ThriftValue::Traits<std::string> {
public:
  static FieldType getFieldType() { return FieldType::String; }
};

using ThriftValuePtr = std::unique_ptr<ThriftValue>;
using ThriftValuePtrList = std::list<ThriftValuePtr>;
using ThriftValuePtrPairList = std::list<std::pair<ThriftValuePtr, ThriftValuePtr>>;

/**
 * ThriftField is a field within a ThriftStruct.
 */
class ThriftField {
public:
  virtual ~ThriftField() = default;

  /**
   * @return FieldType this field's type
   */
  virtual FieldType fieldType() const PURE;

  /**
   * @return int16_t the field's identifier
   */
  virtual int16_t fieldId() const PURE;

  /**
   * @return const ThriftValue& containing the field's value
   */
  virtual const ThriftValue& getValue() const PURE;
};

using ThriftFieldPtr = std::unique_ptr<ThriftField>;
using ThriftFieldPtrList = std::list<ThriftFieldPtr>;

/**
 * ThriftListValue is an ordered list of ThriftValues.
 */
class ThriftListValue {
public:
  virtual ~ThriftListValue() = default;

  /**
   * @return const ThriftValuePtrList& containing the ThriftValues that comprise the list
   */
  virtual const ThriftValuePtrList& elements() const PURE;

  /**
   * @return FieldType of the underlying elements
   */
  virtual FieldType elementType() const PURE;

  /**
   * Used by ThriftValue::Traits to enforce type safety.
   */
  static FieldType getFieldType() { return FieldType::List; }
};

/**
 * ThriftSetValue is a set of ThriftValues, maintained in their original order.
 */
class ThriftSetValue {
public:
  virtual ~ThriftSetValue() = default;

  /**
   * @return const ThriftValuePtrList& containing the ThriftValues that comprise the set
   */
  virtual const ThriftValuePtrList& elements() const PURE;

  /**
   * @return FieldType of the underlying elements
   */
  virtual FieldType elementType() const PURE;

  /**
   * Used by ThriftValue::Traits to enforce type safety.
   */
  static FieldType getFieldType() { return FieldType::Set; }
};

/**
 * ThriftMapValue is a map of pairs of ThriftValues, maintained in their original order.
 */
class ThriftMapValue {
public:
  virtual ~ThriftMapValue() = default;

  /**
   * @return const ThriftValuePtrPairList& containing the ThriftValue key-value pairs that comprise
   *         the map.
   */
  virtual const ThriftValuePtrPairList& elements() const PURE;

  /**
   * @return FieldType of the underlying keys
   */
  virtual FieldType keyType() const PURE;

  /**
   * @return FieldType of the underlying values
   */
  virtual FieldType valueType() const PURE;

  /**
   * Used by ThriftValue::Traits to enforce type safety.
   */
  static FieldType getFieldType() { return FieldType::Map; }
};

/**
 * ThriftStructValue is a sequence of ThriftFields.
 */
class ThriftStructValue {
public:
  virtual ~ThriftStructValue() = default;

  /**
   * @return const ThriftFieldPtrList& containing the ThriftFields that comprise the struct.
   */
  virtual const ThriftFieldPtrList& fields() const PURE;

  /**
   * Used by ThriftValue::Traits to enforce type safety.
   */
  static FieldType getFieldType() { return FieldType::Struct; }
};

/**
 * ThriftObject is a ThriftStructValue that can be read from a Buffer::Instance.
 */
class ThriftObject : public ThriftStructValue {
public:
  ~ThriftObject() override = default;

  /*
   * Consumes bytes from the buffer until a single complete Thrift struct has been consumed.
   * @param buffer starting with a Thrift struct
   * @return true when a single complete struct has been consumed; false if more data is needed to
   *         complete decoding
   * @throw EnvoyException if the struct is invalid
   */
  virtual bool onData(Buffer::Instance& buffer) PURE;
};

using ThriftObjectPtr = std::unique_ptr<ThriftObject>;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
