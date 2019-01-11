#include "extensions/filters/network/thrift_proxy/thrift_object_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

std::unique_ptr<ThriftValueBase> makeValue(ThriftBase* parent, FieldType type) {
  switch (type) {
  case FieldType::Stop:
    NOT_REACHED_GCOVR_EXCL_LINE;

  case FieldType::List:
    return std::make_unique<ThriftListValueImpl>(parent);

  case FieldType::Set:
    return std::make_unique<ThriftSetValueImpl>(parent);

  case FieldType::Map:
    return std::make_unique<ThriftMapValueImpl>(parent);

  case FieldType::Struct:
    return std::make_unique<ThriftStructValueImpl>(parent);

  default:
    return std::make_unique<ThriftValueImpl>(parent, type);
  }
}

} // namespace

ThriftBase::ThriftBase(ThriftBase* parent) : parent_(parent) {}

FilterStatus ThriftBase::structBegin(absl::string_view name) {
  ASSERT(delegate_ != nullptr);
  return delegate_->structBegin(name);
}

FilterStatus ThriftBase::structEnd() {
  ASSERT(delegate_ != nullptr);
  return delegate_->structEnd();
}

FilterStatus ThriftBase::fieldBegin(absl::string_view name, FieldType& field_type,
                                    int16_t& field_id) {
  ASSERT(delegate_ != nullptr);
  return delegate_->fieldBegin(name, field_type, field_id);
}

FilterStatus ThriftBase::fieldEnd() {
  ASSERT(delegate_ != nullptr);
  return delegate_->fieldEnd();
}

FilterStatus ThriftBase::boolValue(bool& value) {
  ASSERT(delegate_ != nullptr);
  return delegate_->boolValue(value);
}

FilterStatus ThriftBase::byteValue(uint8_t& value) {
  ASSERT(delegate_ != nullptr);
  return delegate_->byteValue(value);
}

FilterStatus ThriftBase::int16Value(int16_t& value) {
  ASSERT(delegate_ != nullptr);
  return delegate_->int16Value(value);
}

FilterStatus ThriftBase::int32Value(int32_t& value) {
  ASSERT(delegate_ != nullptr);
  return delegate_->int32Value(value);
}

FilterStatus ThriftBase::int64Value(int64_t& value) {
  ASSERT(delegate_ != nullptr);
  return delegate_->int64Value(value);
}

FilterStatus ThriftBase::doubleValue(double& value) {
  ASSERT(delegate_ != nullptr);
  return delegate_->doubleValue(value);
}

FilterStatus ThriftBase::stringValue(absl::string_view value) {
  ASSERT(delegate_ != nullptr);
  return delegate_->stringValue(value);
}

FilterStatus ThriftBase::mapBegin(FieldType& key_type, FieldType& value_type, uint32_t& size) {
  ASSERT(delegate_ != nullptr);
  return delegate_->mapBegin(key_type, value_type, size);
}

FilterStatus ThriftBase::mapEnd() {
  ASSERT(delegate_ != nullptr);
  return delegate_->mapEnd();
}

FilterStatus ThriftBase::listBegin(FieldType& elem_type, uint32_t& size) {
  ASSERT(delegate_ != nullptr);
  return delegate_->listBegin(elem_type, size);
}

FilterStatus ThriftBase::listEnd() {
  ASSERT(delegate_ != nullptr);
  return delegate_->listEnd();
}

FilterStatus ThriftBase::setBegin(FieldType& elem_type, uint32_t& size) {
  ASSERT(delegate_ != nullptr);
  return delegate_->setBegin(elem_type, size);
}

FilterStatus ThriftBase::setEnd() {
  ASSERT(delegate_ != nullptr);
  return delegate_->setEnd();
}

void ThriftBase::delegateComplete() {
  ASSERT(delegate_ != nullptr);
  delegate_ = nullptr;
}

ThriftFieldImpl::ThriftFieldImpl(ThriftStructValueImpl* parent, absl::string_view name,
                                 FieldType field_type, int16_t field_id)
    : ThriftBase(parent), name_(name), field_type_(field_type), field_id_(field_id) {
  auto value = makeValue(this, field_type_);
  delegate_ = value.get();
  value_ = std::move(value);
}

FilterStatus ThriftFieldImpl::fieldEnd() {
  if (delegate_) {
    return delegate_->fieldEnd();
  }

  parent_->delegateComplete();
  return FilterStatus::Continue;
}

FilterStatus ThriftListValueImpl::listBegin(FieldType& elem_type, uint32_t& size) {
  if (delegate_) {
    return delegate_->listBegin(elem_type, size);
  }

  elem_type_ = elem_type;
  remaining_ = size;

  delegateComplete();

  return FilterStatus::Continue;
}

FilterStatus ThriftListValueImpl::listEnd() {
  if (delegate_) {
    return delegate_->listEnd();
  }

  ASSERT(remaining_ == 0);
  parent_->delegateComplete();
  return FilterStatus::Continue;
}

void ThriftListValueImpl::delegateComplete() {
  delegate_ = nullptr;

  if (remaining_ == 0) {
    return;
  }

  auto elem = makeValue(this, elem_type_);
  delegate_ = elem.get();
  elements_.push_back(std::move(elem));
  remaining_--;
}

FilterStatus ThriftSetValueImpl::setBegin(FieldType& elem_type, uint32_t& size) {
  if (delegate_) {
    return delegate_->setBegin(elem_type, size);
  }

  elem_type_ = elem_type;
  remaining_ = size;

  delegateComplete();

  return FilterStatus::Continue;
}

FilterStatus ThriftSetValueImpl::setEnd() {
  if (delegate_) {
    return delegate_->setEnd();
  }

  ASSERT(remaining_ == 0);
  parent_->delegateComplete();
  return FilterStatus::Continue;
}

void ThriftSetValueImpl::delegateComplete() {
  delegate_ = nullptr;

  if (remaining_ == 0) {
    return;
  }

  auto elem = makeValue(this, elem_type_);
  delegate_ = elem.get();
  elements_.push_back(std::move(elem));
  remaining_--;
}

FilterStatus ThriftMapValueImpl::mapBegin(FieldType& key_type, FieldType& elem_type,
                                          uint32_t& size) {
  if (delegate_) {
    return delegate_->mapBegin(key_type, elem_type, size);
  }

  key_type_ = key_type;
  elem_type_ = elem_type;
  remaining_ = size;

  delegateComplete();

  return FilterStatus::Continue;
}

FilterStatus ThriftMapValueImpl::mapEnd() {
  if (delegate_) {
    return delegate_->mapEnd();
  }

  ASSERT(remaining_ == 0);
  parent_->delegateComplete();
  return FilterStatus::Continue;
}

void ThriftMapValueImpl::delegateComplete() {
  delegate_ = nullptr;

  if (remaining_ == 0) {
    return;
  }

  // Prepare for first element's key.
  if (elements_.empty()) {
    auto key = makeValue(this, key_type_);
    delegate_ = key.get();
    elements_.emplace_back(std::move(key), nullptr);
    return;
  }

  // Prepare for any elements's value.
  auto& elem = elements_.back();
  if (elem.second == nullptr) {
    auto value = makeValue(this, elem_type_);
    delegate_ = value.get();
    elem.second = std::move(value);

    remaining_--;
    return;
  }

  // Key-value pair completed, prepare for next key.
  auto key = makeValue(this, key_type_);
  delegate_ = key.get();
  elements_.emplace_back(std::move(key), nullptr);
}

FilterStatus ThriftValueImpl::boolValue(bool& value) {
  ASSERT(value_type_ == FieldType::Bool);
  bool_value_ = value;
  parent_->delegateComplete();
  return FilterStatus::Continue;
}

FilterStatus ThriftValueImpl::byteValue(uint8_t& value) {
  ASSERT(value_type_ == FieldType::Byte);
  byte_value_ = value;
  parent_->delegateComplete();
  return FilterStatus::Continue;
}

FilterStatus ThriftValueImpl::int16Value(int16_t& value) {
  ASSERT(value_type_ == FieldType::I16);
  int16_value_ = value;
  parent_->delegateComplete();
  return FilterStatus::Continue;
}

FilterStatus ThriftValueImpl::int32Value(int32_t& value) {
  ASSERT(value_type_ == FieldType::I32);
  int32_value_ = value;
  parent_->delegateComplete();
  return FilterStatus::Continue;
}

FilterStatus ThriftValueImpl::int64Value(int64_t& value) {
  ASSERT(value_type_ == FieldType::I64);
  int64_value_ = value;
  parent_->delegateComplete();
  return FilterStatus::Continue;
}

FilterStatus ThriftValueImpl::doubleValue(double& value) {
  ASSERT(value_type_ == FieldType::Double);
  double_value_ = value;
  parent_->delegateComplete();
  return FilterStatus::Continue;
}

FilterStatus ThriftValueImpl::stringValue(absl::string_view value) {
  ASSERT(value_type_ == FieldType::String);
  string_value_ = std::string(value);
  parent_->delegateComplete();
  return FilterStatus::Continue;
}

const void* ThriftValueImpl::getValue() const {
  switch (value_type_) {
  case FieldType::Bool:
    return &bool_value_;
  case FieldType::Byte:
    return &byte_value_;
  case FieldType::I16:
    return &int16_value_;
  case FieldType::I32:
    return &int32_value_;
  case FieldType::I64:
    return &int64_value_;
  case FieldType::Double:
    return &double_value_;
  case FieldType::String:
    return &string_value_;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

FilterStatus ThriftStructValueImpl::structBegin(absl::string_view name) {
  if (delegate_) {
    return delegate_->structBegin(name);
  }

  return FilterStatus::Continue;
}

FilterStatus ThriftStructValueImpl::structEnd() {
  if (delegate_) {
    return delegate_->structEnd();
  }

  if (parent_) {
    parent_->delegateComplete();
  }

  return FilterStatus::Continue;
}

FilterStatus ThriftStructValueImpl::fieldBegin(absl::string_view name, FieldType& field_type,
                                               int16_t& field_id) {
  if (delegate_) {
    return delegate_->fieldBegin(name, field_type, field_id);
  }

  if (field_type != FieldType::Stop) {
    auto field = std::make_unique<ThriftFieldImpl>(this, name, field_type, field_id);
    delegate_ = field.get();
    fields_.emplace_back(std::move(field));
  }

  return FilterStatus::Continue;
}

ThriftObjectImpl::ThriftObjectImpl(Transport& transport, Protocol& protocol)
    : ThriftStructValueImpl(nullptr),
      decoder_(std::make_unique<Decoder>(transport, protocol, *this)) {}

bool ThriftObjectImpl::onData(Buffer::Instance& buffer) {
  bool underflow = false;
  auto result = decoder_->onData(buffer, underflow);
  ASSERT(result == FilterStatus::Continue);

  if (complete_) {
    decoder_.reset();
  }
  return complete_;
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
