#pragma once

#include "extensions/filters/network/thrift_proxy/decoder.h"
#include "extensions/filters/network/thrift_proxy/filters/filter.h"
#include "extensions/filters/network/thrift_proxy/thrift_object.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * ThriftBase is a base class for decoding Thrift objects. It implements methods from
 * DecoderEventHandler to automatically delegate to an underlying ThriftBase so that, for example,
 * the fieldBegin call for a struct field nested within a list is automatically delegated down the
 * object hierarchy to the correct ThriftBase subclass.
 */
class ThriftBase : public DecoderEventHandler {
public:
  ThriftBase(ThriftBase* parent);
  ~ThriftBase() {}

  // DecoderEventHandler
  FilterStatus transportBegin(MessageMetadataSharedPtr) override { return FilterStatus::Continue; }
  FilterStatus transportEnd() override { return FilterStatus::Continue; }
  FilterStatus messageBegin(MessageMetadataSharedPtr) override { return FilterStatus::Continue; }
  FilterStatus messageEnd() override { return FilterStatus::Continue; }
  FilterStatus structBegin(absl::string_view name) override;
  FilterStatus structEnd() override;
  FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                          int16_t& field_id) override;
  FilterStatus fieldEnd() override;
  FilterStatus boolValue(bool& value) override;
  FilterStatus byteValue(uint8_t& value) override;
  FilterStatus int16Value(int16_t& value) override;
  FilterStatus int32Value(int32_t& value) override;
  FilterStatus int64Value(int64_t& value) override;
  FilterStatus doubleValue(double& value) override;
  FilterStatus stringValue(absl::string_view value) override;
  FilterStatus mapBegin(FieldType& key_type, FieldType& value_type, uint32_t& size) override;
  FilterStatus mapEnd() override;
  FilterStatus listBegin(FieldType& elem_type, uint32_t& size) override;
  FilterStatus listEnd() override;
  FilterStatus setBegin(FieldType& elem_type, uint32_t& size) override;
  FilterStatus setEnd() override;

  // Invoked when the current delegate is complete. Completion implies that the delegate is fully
  // specified (all list values processed, all struct fields processed, etc).
  virtual void delegateComplete();

protected:
  ThriftBase* parent_;
  ThriftBase* delegate_{nullptr};
};

/**
 * ThriftValueBase is a base class for all struct field values, list values, set values, map keys,
 * and map values.
 */
class ThriftValueBase : public ThriftValue, public ThriftBase {
public:
  ThriftValueBase(ThriftBase* parent, FieldType value_type)
      : ThriftBase(parent), value_type_(value_type) {}
  ~ThriftValueBase() {}

  // ThriftValue
  FieldType type() const override { return value_type_; }

protected:
  const FieldType value_type_;
};

class ThriftStructValueImpl;

/**
 * ThriftField represents a field in a thrift Struct. It always delegates DecoderEventHandler
 * methods to a subclass of ThriftValueBase.
 */
class ThriftFieldImpl : public ThriftField, public ThriftBase {
public:
  ThriftFieldImpl(ThriftStructValueImpl* parent, absl::string_view name, FieldType field_type,
                  int16_t field_id);

  // DecoderEventHandler
  FilterStatus fieldEnd() override;

  // ThriftField
  FieldType fieldType() const override { return field_type_; }
  int16_t fieldId() const override { return field_id_; }
  const ThriftValue& getValue() const override { return *value_; }

private:
  std::string name_;
  FieldType field_type_;
  int16_t field_id_;
  ThriftValuePtr value_;
};

/**
 * ThriftStructValueImpl implements ThriftStruct.
 */
class ThriftStructValueImpl : public ThriftStructValue, public ThriftValueBase {
public:
  ThriftStructValueImpl(ThriftBase* parent) : ThriftValueBase(parent, FieldType::Struct) {}

  // DecoderEventHandler
  FilterStatus structBegin(absl::string_view name) override;
  FilterStatus structEnd() override;
  FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                          int16_t& field_id) override;

  // ThriftStructValue
  const ThriftFieldPtrList& fields() const override { return fields_; }

private:
  // ThriftValue
  const void* getValue() const override { return this; };

  ThriftFieldPtrList fields_;
};

/**
 * ThriftListValueImpl represents Thrift lists.
 */
class ThriftListValueImpl : public ThriftListValue, public ThriftValueBase {
public:
  ThriftListValueImpl(ThriftBase* parent) : ThriftValueBase(parent, FieldType::List) {}

  // DecoderEventHandler
  FilterStatus listBegin(FieldType& elem_type, uint32_t& size) override;
  FilterStatus listEnd() override;

  // ThriftListValue
  const ThriftValuePtrList& elements() const override { return elements_; }
  FieldType elementType() const override { return elem_type_; }

  void delegateComplete() override;

protected:
  // ThriftValue
  const void* getValue() const override { return this; };

  FieldType elem_type_{FieldType::Stop};
  uint32_t remaining_{0};
  ThriftValuePtrList elements_;
};

/**
 * ThriftSetValueImpl represents Thrift sets.
 */
class ThriftSetValueImpl : public ThriftSetValue, public ThriftValueBase {
public:
  ThriftSetValueImpl(ThriftBase* parent) : ThriftValueBase(parent, FieldType::Set) {}

  // DecoderEventHandler
  FilterStatus setBegin(FieldType& elem_type, uint32_t& size) override;
  FilterStatus setEnd() override;

  // ThriftSetValue
  const ThriftValuePtrList& elements() const override { return elements_; }
  FieldType elementType() const override { return elem_type_; }

  void delegateComplete() override;

protected:
  // ThriftValue
  const void* getValue() const override { return this; };

  FieldType elem_type_{FieldType::Stop};
  uint32_t remaining_{0};
  ThriftValuePtrList elements_; // maintain original order
};

/**
 * ThriftMapValueImpl represents Thrift maps.
 */
class ThriftMapValueImpl : public ThriftMapValue, public ThriftValueBase {
public:
  ThriftMapValueImpl(ThriftBase* parent) : ThriftValueBase(parent, FieldType::Map) {}

  // DecoderEventHandler
  FilterStatus mapBegin(FieldType& key_type, FieldType& elem_type, uint32_t& size) override;
  FilterStatus mapEnd() override;

  // ThriftMapValue
  const ThriftValuePtrPairList& elements() const override { return elements_; }
  FieldType keyType() const override { return key_type_; }
  FieldType valueType() const override { return elem_type_; }

  void delegateComplete() override;

protected:
  // ThriftValue
  const void* getValue() const override { return this; };

  FieldType key_type_{FieldType::Stop};
  FieldType elem_type_{FieldType::Stop};
  uint32_t remaining_{0};
  ThriftValuePtrPairList elements_; // maintain original order
};

/**
 * ThriftValueImpl represents primitive Thrift types, including strings.
 */
class ThriftValueImpl : public ThriftValueBase {
public:
  ThriftValueImpl(ThriftBase* parent, FieldType value_type) : ThriftValueBase(parent, value_type) {}

  // DecoderEventHandler
  FilterStatus boolValue(bool& value) override;
  FilterStatus byteValue(uint8_t& value) override;
  FilterStatus int16Value(int16_t& value) override;
  FilterStatus int32Value(int32_t& value) override;
  FilterStatus int64Value(int64_t& value) override;
  FilterStatus doubleValue(double& value) override;
  FilterStatus stringValue(absl::string_view value) override;

protected:
  // ThriftValue
  const void* getValue() const override;

private:
  union {
    bool bool_value_;
    uint8_t byte_value_;
    int16_t int16_value_;
    int32_t int32_value_;
    int64_t int64_value_;
    double double_value_;
  };
  std::string string_value_;
};

/**
 * ThriftObjectImpl is a generic representation of a Thrift struct.
 */
class ThriftObjectImpl : public ThriftObject,
                         public ThriftStructValueImpl,
                         public DecoderCallbacks {
public:
  ThriftObjectImpl(Transport& transport, Protocol& protocol);

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler() override { return *this; }
  FilterStatus transportEnd() override {
    complete_ = true;
    return FilterStatus::Continue;
  }

  // ThriftObject
  bool onData(Buffer::Instance& buffer) override;

  // ThriftStruct
  const ThriftFieldPtrList& fields() const override { return ThriftStructValueImpl::fields(); }

private:
  DecoderPtr decoder_;
  bool complete_{false};
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
