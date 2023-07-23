#pragma once

#include "source/extensions/filters/network/thrift_proxy/metadata.h"
#include "source/extensions/filters/network/thrift_proxy/thrift.h"

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

enum class DecoderEvent {
  TransportBegin,
  TransportEnd,
  PassthroughData,
  MessageBegin,
  MessageEnd,
  StructBegin,
  StructEnd,
  FieldBegin,
  FieldEnd,
  BoolValue,
  ByteValue,
  DoubleValue,
  Int16Value,
  Int32Value,
  Int64Value,
  StringValue,
  ListBegin,
  ListEnd,
  SetBegin,
  SetEnd,
  MapBegin,
  MapEnd,
  ContinueDecode
};

class DecoderEventHandler {
public:
  virtual ~DecoderEventHandler() = default;

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
   * Indicates raw bytes after metadata in a Thrift transport frame was detected.
   * Filters should not modify data except for the router.
   * @param data data to send as passthrough
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus passthroughData(Buffer::Instance& data) PURE;

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
  virtual FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                                  int16_t& field_id) PURE;

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
  virtual FilterStatus boolValue(bool& value) PURE;
  virtual FilterStatus byteValue(uint8_t& value) PURE;
  virtual FilterStatus int16Value(int16_t& value) PURE;
  virtual FilterStatus int32Value(int32_t& value) PURE;
  virtual FilterStatus int64Value(int64_t& value) PURE;
  virtual FilterStatus doubleValue(double& value) PURE;
  virtual FilterStatus stringValue(absl::string_view value) PURE;

  /**
   * Indicates the start of a Thrift protocol map was detected.
   * @param key_type the map key type
   * @param value_type the map value type
   * @param size the number of key-value pairs
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus mapBegin(FieldType& key_type, FieldType& value_type, uint32_t& size) PURE;

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
  virtual FilterStatus listBegin(FieldType& elem_type, uint32_t& size) PURE;

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
  virtual FilterStatus setBegin(FieldType& elem_type, uint32_t& size) PURE;

  /**
   * Indicates that the end of a Thrift protocol set was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus setEnd() PURE;
};

using DecoderEventHandlerSharedPtr = std::shared_ptr<DecoderEventHandler>;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
