#pragma once

#include "extensions/filters/network/thrift_proxy/metadata.h"
#include "extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

enum class FilterStatus {
  // Continue filter chain iteration.
  Continue,

  // Stop iterating over filters in the filter chain. Iteration must be explicitly restarted via
  // continueDecoding().
  StopIteration
};

class DecoderEventHandler {
public:
  virtual ~DecoderEventHandler() {}

  /**
   * Indicates the start of a Thrift transport frame was detected. Unframed transports generate
   * simulated start messages.
   * @param metadata MessageMetadataSharedPtr describing as much as is currently known about the
   *                                          message
   */
  virtual FilterStatus transportBegin(MessageMetadataSharedPtr metadata) PURE;

  /**
   * Indicates the end of a Thrift transport frame was detected. Unframed transport generate
   * simulated complete messages.
   */
  virtual FilterStatus transportEnd() PURE;

  /**
   * Indicates that the start of a Thrift protocol message was detected.
   * @param metadata MessageMetadataSharedPtr describing the message
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus messageBegin(MessageMetadataSharedPtr metadata) PURE;

  /**
   * Indicates that the end of a Thrift protocol message was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus messageEnd() PURE;

  /**
   * Indicates that the start of a Thrift protocol struct was detected.
   * @param name the name of the struct, if available
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus structBegin(absl::string_view name) PURE;

  /**
   * Indicates that the end of a Thrift protocol struct was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus structEnd() PURE;

  /**
   * Indicates that the start of Thrift protocol struct field was detected.
   * @param name the name of the field, if available
   * @param field_type the type of the field
   * @param field_id the field id
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus fieldBegin(absl::string_view name, FieldType field_type,
                                  int16_t field_id) PURE;

  /**
   * Indicates that the end of a Thrift protocol struct field was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus fieldEnd() PURE;

  /**
   * A struct field, map key, map value, list element or set element was detected.
   * @param value type value of the field
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus boolValue(bool value) PURE;
  virtual FilterStatus byteValue(uint8_t value) PURE;
  virtual FilterStatus int16Value(int16_t value) PURE;
  virtual FilterStatus int32Value(int32_t value) PURE;
  virtual FilterStatus int64Value(int64_t value) PURE;
  virtual FilterStatus doubleValue(double value) PURE;
  virtual FilterStatus stringValue(absl::string_view value) PURE;

  /**
   * Indicates the start of a Thrift protocol map was detected.
   * @param key_type the map key type
   * @param value_type the map value type
   * @param size the number of key-value pairs
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus mapBegin(FieldType key_type, FieldType value_type, uint32_t size) PURE;

  /**
   * Indicates that the end of a Thrift protocol map was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus mapEnd() PURE;

  /**
   * Indicates the start of a Thrift protocol list was detected.
   * @param elem_type the list value type
   * @param size the number of values in the list
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus listBegin(FieldType elem_type, uint32_t size) PURE;

  /**
   * Indicates that the end of a Thrift protocol list was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus listEnd() PURE;

  /**
   * Indicates the start of a Thrift protocol set was detected.
   * @param elem_type the set value type
   * @param size the number of values in the set
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus setBegin(FieldType elem_type, uint32_t size) PURE;

  /**
   * Indicates that the end of a Thrift protocol set was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus setEnd() PURE;
};

typedef std::shared_ptr<DecoderEventHandler> DecoderEventHandlerSharedPtr;

class DelegatingDecoderEventHandler : public virtual DecoderEventHandler {
public:
  virtual ~DelegatingDecoderEventHandler() {}

  // DecoderEventHandler
  FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override {
    return event_handler_->transportBegin(metadata);
  }
  FilterStatus transportEnd() override { return event_handler_->transportEnd(); }
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override {
    return event_handler_->messageBegin(metadata);
  };
  FilterStatus messageEnd() override { return event_handler_->messageEnd(); }
  FilterStatus structBegin(absl::string_view name) override {
    return event_handler_->structBegin(name);
  }
  FilterStatus structEnd() override { return event_handler_->structEnd(); }
  FilterStatus fieldBegin(absl::string_view name, FieldType field_type, int16_t field_id) override {
    return event_handler_->fieldBegin(name, field_type, field_id);
  }
  FilterStatus fieldEnd() override { return event_handler_->fieldEnd(); }
  FilterStatus boolValue(bool value) override { return event_handler_->boolValue(value); }
  FilterStatus byteValue(uint8_t value) override { return event_handler_->byteValue(value); }
  FilterStatus int16Value(int16_t value) override { return event_handler_->int16Value(value); }
  FilterStatus int32Value(int32_t value) override { return event_handler_->int32Value(value); }
  FilterStatus int64Value(int64_t value) override { return event_handler_->int64Value(value); }
  FilterStatus doubleValue(double value) override { return event_handler_->doubleValue(value); }
  FilterStatus stringValue(absl::string_view value) override {
    return event_handler_->stringValue(value);
  }
  FilterStatus mapBegin(FieldType key_type, FieldType value_type, uint32_t size) override {
    return event_handler_->mapBegin(key_type, value_type, size);
  }
  FilterStatus mapEnd() override { return event_handler_->mapEnd(); }
  FilterStatus listBegin(FieldType elem_type, uint32_t size) override {
    return event_handler_->listBegin(elem_type, size);
  }
  FilterStatus listEnd() override { return event_handler_->listEnd(); }
  FilterStatus setBegin(FieldType elem_type, uint32_t size) override {
    return event_handler_->setBegin(elem_type, size);
  }
  FilterStatus setEnd() override { return event_handler_->setEnd(); }

protected:
  DecoderEventHandler* event_handler_{};
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
