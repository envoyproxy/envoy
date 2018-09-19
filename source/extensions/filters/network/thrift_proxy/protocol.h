#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/config/utility.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/network/thrift_proxy/conn_state.h"
#include "extensions/filters/network/thrift_proxy/decoder_events.h"
#include "extensions/filters/network/thrift_proxy/metadata.h"
#include "extensions/filters/network/thrift_proxy/thrift.h"
#include "extensions/filters/network/thrift_proxy/thrift_object.h"
#include "extensions/filters/network/thrift_proxy/transport.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class DirectResponse;
typedef std::unique_ptr<DirectResponse> DirectResponsePtr;

/**
 * Protocol represents the operations necessary to implement the a generic Thrift protocol.
 * See https://github.com/apache/thrift/blob/master/doc/specs/thrift-protocol-spec.md
 */
class Protocol {
public:
  virtual ~Protocol() {}

  /**
   * @return const std::string& the human-readable name of the protocol
   */
  virtual const std::string& name() const PURE;

  /**
   * @return ProtocolType the protocol type
   */
  virtual ProtocolType type() const PURE;

  /**
   * For protocol-detecting implementations, set the underlying type based on external
   * (e.g. transport-level) information).
   * @param type ProtocolType to explicitly set
   */
  virtual void setType(ProtocolType) { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  /**
   * Reads the start of a Thrift protocol message from the buffer and updates the metadata
   * parameter with values from the message header. If successful, the message header is removed
   * from the buffer.
   * @param buffer the buffer to read from
   * @param metadata MessageMetadata to be updated with name, message type, and sequence id.
   * @return true if a message header was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid message header
   */
  virtual bool readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) PURE;

  /**
   * Reads the end of a Thrift protocol message from the buffer. If successful, the message footer
   * is removed from the buffer.
   * @param buffer the buffer to read from
   * @return true if a message footer was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid message footer
   */
  virtual bool readMessageEnd(Buffer::Instance& buffer) PURE;

  /**
   * Reads the start of a Thrift struct from the buffer and updates the name parameter with the
   * value from the struct header. If successful, the struct header is removed from the buffer.
   * @param buffer the buffer to read from
   * @param name updated with the struct name on success only
   * @return true if a struct header was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid struct header
   */
  virtual bool readStructBegin(Buffer::Instance& buffer, std::string& name) PURE;

  /**
   * Reads the end of a Thrift struct from the buffer. If successful, the struct footer is removed
   * from the buffer.
   * @param buffer the buffer to read from
   * @return true if a struct footer was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid struct footer
   */
  virtual bool readStructEnd(Buffer::Instance& buffer) PURE;

  /**
   * Reads the start of a Thrift struct field from the buffer and updates the name, field_type, and
   * field_id parameters with the values from the field header. If successful, the field header is
   * removed from the buffer.
   * @param buffer the buffer to read from
   * @param name updated with the field name on success only
   * @param field_type updated with the FieldType on success only
   * @param field_id updated with the field ID on success only
   * @return true if a field header was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid field header
   */
  virtual bool readFieldBegin(Buffer::Instance& buffer, std::string& name, FieldType& field_type,
                              int16_t& field_id) PURE;

  /**
   * Reads the end of a Thrift struct field from the buffer. If successful, the field footer is
   * removed from the buffer.
   * @param buffer the buffer to read from
   * @return true if a field footer was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid field footer
   */
  virtual bool readFieldEnd(Buffer::Instance& buffer) PURE;

  /**
   * Reads the start of a Thrift map from the buffer and updates the key_type, value_type, and size
   * parameters with the values from the map header. If successful, the map header is removed from
   * the buffer.
   * @param buffer the buffer to read from
   * @param key_type updated with map key FieldType on success only
   * @param value_type updated with map value FieldType on success only
   * @param size updated with the number of key-value pairs in the map on success only
   * @return true if a map header was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid map header
   */
  virtual bool readMapBegin(Buffer::Instance& buffer, FieldType& key_type, FieldType& value_type,
                            uint32_t& size) PURE;

  /**
   * Reads the end of a Thrift map from the buffer. If successful, the map footer is removed from
   * the buffer.
   * @param buffer the buffer to read from
   * @return true if a map footer was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid map footer
   */
  virtual bool readMapEnd(Buffer::Instance& buffer) PURE;

  /**
   * Reads the start of a Thrift list from the buffer and updates the elem_type, and size
   * parameters with the values from the list header. If successful, the list header is removed from
   * the buffer.
   * @param buffer the buffer to read from
   * @param elem_type updated with list element FieldType on success only
   * @param size updated with the number of list members on success only
   * @return true if a list header was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid list header
   */
  virtual bool readListBegin(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size) PURE;

  /**
   * Reads the end of a Thrift list from the buffer. If successful, the list footer is removed from
   * the buffer.
   * @param buffer the buffer to read from
   * @return true if a list footer was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid list footer
   */
  virtual bool readListEnd(Buffer::Instance& buffer) PURE;

  /**
   * Reads the start of a Thrift set from the buffer and updates the elem_type, and size
   * parameters with the values from the set header. If successful, the set header is removed from
   * the buffer.
   * @param buffer the buffer to read from
   * @param elem_type updated with set element FieldType on success only
   * @param size updated with the number of set members on success only
   * @return true if a set header was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid set header
   */
  virtual bool readSetBegin(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size) PURE;

  /**
   * Reads the end of a Thrift set from the buffer. If successful, the set footer is removed from
   * the buffer.
   * @param buffer the buffer to read from
   * @return true if a set footer was sucessfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid set footer
   */
  virtual bool readSetEnd(Buffer::Instance& buffer) PURE;

  /**
   * Reads a boolean value from the buffer and updates value. If successful, the value is removed
   * from the buffer.
   * @param buffer the buffer to read from
   * @param value updated with the value read from the buffer
   * @return true if a value successfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid set footer
   */
  virtual bool readBool(Buffer::Instance& buffer, bool& value) PURE;

  /**
   * Reads a byte value from the buffer and updates value. If successful, the value is removed from
   * the buffer.
   * @param buffer the buffer to read from
   * @param value updated with the value read from the buffer
   * @return true if a value successfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid set footer
   */
  virtual bool readByte(Buffer::Instance& buffer, uint8_t& value) PURE;

  /**
   * Reads a int16_t value from the buffer and updates value. If successful, the value is removed
   * from the buffer.
   * @param buffer the buffer to read from
   * @param value updated with the value read from the buffer
   * @return true if a value successfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid set footer
   */
  virtual bool readInt16(Buffer::Instance& buffer, int16_t& value) PURE;

  /**
   * Reads a int32_t value from the buffer and updates value. If successful, the value is removed
   * from the buffer.
   * @param buffer the buffer to read from
   * @param value updated with the value read from the buffer
   * @return true if a value successfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid set footer
   */
  virtual bool readInt32(Buffer::Instance& buffer, int32_t& value) PURE;

  /**
   * Reads a int64_t value from the buffer and updates value. If successful, the value is removed
   * from the buffer.
   * @param buffer the buffer to read from
   * @param value updated with the value read from the buffer
   * @return true if a value successfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid set footer
   */
  virtual bool readInt64(Buffer::Instance& buffer, int64_t& value) PURE;

  /**
   * Reads a double value from the buffer and updates value. If successful, the value is removed
   * from the buffer.
   * @param buffer the buffer to read from
   * @param value updated with the value read from the buffer
   * @return true if a value successfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid set footer
   */
  virtual bool readDouble(Buffer::Instance& buffer, double& value) PURE;

  /**
   * Reads a string value from the buffer and updates value. If successful, the value is removed
   * from the buffer.
   * @param buffer the buffer to read from
   * @param value updated with the value read from the buffer
   * @return true if a value successfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid set footer
   */
  virtual bool readString(Buffer::Instance& buffer, std::string& value) PURE;

  /**
   * Reads a binary value from the buffer and updates value. If successful, the value is removed
   * from the buffer.
   * @param buffer the buffer to read from
   * @param value updated with the value read from the buffer
   * @return true if a value successfully read, false if more data is required
   * @throw EnvoyException if the data is not a valid set footer
   */
  virtual bool readBinary(Buffer::Instance& buffer, std::string& value) PURE;

  /**
   * Writes the start of a Thrift protocol message to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param metadata MessageMetadata for the message to write.
   */
  virtual void writeMessageBegin(Buffer::Instance& buffer, const MessageMetadata& metadata) PURE;

  /**
   * Writes the end of a Thrift protocol message to the buffer.
   * @param buffer Buffer::Instance to modify
   */
  virtual void writeMessageEnd(Buffer::Instance& buffer) PURE;

  /**
   * Writes the start of a Thrift struct to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param name the struct name, if known
   */
  virtual void writeStructBegin(Buffer::Instance& buffer, const std::string& name) PURE;

  /**
   * Writes the end of a Thrift struct to the buffer.
   * @param buffer Buffer::Instance to modify
   */
  virtual void writeStructEnd(Buffer::Instance& buffer) PURE;

  /**
   * Writes the start of a Thrift struct field to the buffer
   * @param buffer Buffer::Instance to modify
   * @param name the field name, if known
   * @param field_type the field's FieldType
   * @param field_id the field ID
   */
  virtual void writeFieldBegin(Buffer::Instance& buffer, const std::string& name,
                               FieldType field_type, int16_t field_id) PURE;

  /**
   * Writes the end of a Thrift struct field to the buffer.
   * @param buffer Buffer::Instance to modify
   */
  virtual void writeFieldEnd(Buffer::Instance& buffer) PURE;

  /**
   * Writes the start of a Thrift map to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param key_type the map key FieldType
   * @param value_type the map value FieldType
   * @param size the number of key-value pairs in the map
   */
  virtual void writeMapBegin(Buffer::Instance& buffer, FieldType key_type, FieldType value_type,
                             uint32_t size) PURE;

  /**
   * Writes the end of a Thrift map to the buffer.
   * @param buffer Buffer::Instance to modify
   */
  virtual void writeMapEnd(Buffer::Instance& buffer) PURE;

  /**
   * Writes the start of a Thrift list to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param elem_type the list element FieldType
   * @param size the number of list members
   */
  virtual void writeListBegin(Buffer::Instance& buffer, FieldType elem_type, uint32_t size) PURE;

  /**
   * Writes the end of a Thrift list to the buffer.
   * @param buffer Buffer::Instance to modify
   */
  virtual void writeListEnd(Buffer::Instance& buffer) PURE;

  /**
   * Writes the start of a Thrift set to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param elem_type the set element FieldType
   * @param size the number of set members
   */
  virtual void writeSetBegin(Buffer::Instance& buffer, FieldType elem_type, uint32_t size) PURE;

  /**
   * Writes the end of a Thrift set to the buffer.
   * @param buffer Buffer::Instance to modify
   */
  virtual void writeSetEnd(Buffer::Instance& buffer) PURE;

  /**
   * Writes a boolean value to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param value bool to write
   */
  virtual void writeBool(Buffer::Instance& buffer, bool value) PURE;

  /**
   * Writes a byte value to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param value uint8_t to write
   */
  virtual void writeByte(Buffer::Instance& buffer, uint8_t value) PURE;

  /**
   * Writes a int16_t value to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param value int16_t to write
   */
  virtual void writeInt16(Buffer::Instance& buffer, int16_t value) PURE;

  /**
   * Writes a int32_t value to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param value int32_t to write
   */
  virtual void writeInt32(Buffer::Instance& buffer, int32_t value) PURE;

  /**
   * Writes a int64_t value to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param value int64_t to write
   */
  virtual void writeInt64(Buffer::Instance& buffer, int64_t value) PURE;

  /**
   * Writes a double value to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param value double to write
   */
  virtual void writeDouble(Buffer::Instance& buffer, double value) PURE;

  /**
   * Writes a string value to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param value std::string to write
   */
  virtual void writeString(Buffer::Instance& buffer, const std::string& value) PURE;

  /**
   * Writes a binary value to the buffer.
   * @param buffer Buffer::Instance to modify
   * @param value std::string to write
   */
  virtual void writeBinary(Buffer::Instance& buffer, const std::string& value) PURE;

  /**
   * Indicates whether a protocol uses start-of-connection messages to negotiate protocol options.
   * If this method returns true, the Protocol must invoke setProtocolUpgradeMessage during
   * readMessageBegin if it detects an upgrade request.
   *
   * @return true for protocols that exchange messages at the start of a connection to negotiate
   *         protocol upgrade (or options)
   */
  virtual bool supportsUpgrade() { return false; }

  /**
   * Creates an opaque DecoderEventHandlerSharedPtr that can decode a downstream client's upgrade
   * request. When the request is complete, the decoder is passed back to writeUpgradeResponse
   * to allow the Protocol to update its internal state and generate a response to the request.
   *
   * @return a DecoderEventHandlerSharedPtr that decodes a downstream client's upgrade request
   */
  virtual DecoderEventHandlerSharedPtr upgradeRequestDecoder() { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  /**
   * Writes a response to a downstream client's upgrade request.
   * @param decoder DecoderEventHandlerSharedPtr created by upgradeRequestDecoder
   * @return DirectResponsePtr containing an upgrade response
   */
  virtual DirectResponsePtr upgradeResponse(const DecoderEventHandler& decoder) {
    UNREFERENCED_PARAMETER(decoder);
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  /**
   * Checks whether a given upstream connection can be upgraded and generates an upgrade request
   * message. If this method returns a ThriftObject it will be used to decode the upstream's next
   * response.
   *
   * @param transport the Transport to use for decoding the response
   * @param state ThriftConnectionState tracking whether upgrade has already been performed
   * @param buffer Buffer::Instance to modify with an upgrade request
   * @return a ThriftObject capable of decoding an upgrade response or nullptr if upgrade was
   *         already completed (successfully or not)
   */
  virtual ThriftObjectPtr attemptUpgrade(Transport& transport, ThriftConnectionState& state,
                                         Buffer::Instance& buffer) {
    UNREFERENCED_PARAMETER(transport);
    UNREFERENCED_PARAMETER(state);
    UNREFERENCED_PARAMETER(buffer);
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  /**
   * Completes an upgrade previously started via attemptUpgrade.
   * @param response ThriftObject created by attemptUpgrade, after the response has completed
   *        decoding
   */
  virtual void completeUpgrade(ThriftConnectionState& state, ThriftObject& response) {
    UNREFERENCED_PARAMETER(state);
    UNREFERENCED_PARAMETER(response);
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
};

typedef std::unique_ptr<Protocol> ProtocolPtr;

/**
 * A DirectResponse manipulates a Protocol to directly create a Thrift response message.
 */
class DirectResponse {
public:
  virtual ~DirectResponse() {}

  enum class ResponseType {
    // DirectResponse encodes MessageType::Reply with success payload
    SuccessReply,

    // DirectResponse encodes MessageType::Reply with an exception payload
    ErrorReply,

    // DirectResponse encodes MessageType::Exception
    Exception,
  };

  /**
   * Encodes the response via the given Protocol.
   * @param metadata the MessageMetadata for the request that generated this response
   * @param proto the Protocol to be used for message encoding
   * @param buffer the Buffer into which the message should be encoded
   * @return ResponseType indicating whether the message is a successful or error reply or an
   *         exception
   */
  virtual ResponseType encode(MessageMetadata& metadata, Protocol& proto,
                              Buffer::Instance& buffer) const PURE;
};

/**
 * Implemented by each Thrift protocol and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedProtocolConfigFactory {
public:
  virtual ~NamedProtocolConfigFactory() {}

  /**
   * Create a particular Thrift protocol
   * @return ProtocolFactoryCb the protocol
   */
  virtual ProtocolPtr createProtocol() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of thrift protocol
   * produced by the factory.
   */
  virtual std::string name() PURE;

  /**
   * Convenience method to lookup a factory by type.
   * @param ProtocolType the protocol type
   * @return NamedProtocolConfigFactory& for the ProtocolType
   */
  static NamedProtocolConfigFactory& getFactory(ProtocolType type) {
    const std::string& name = ProtocolNames::get().fromType(type);
    return Envoy::Config::Utility::getAndCheckFactory<NamedProtocolConfigFactory>(name);
  }
};

/**
 * ProtocolFactoryBase provides a template for a trivial NamedProtocolConfigFactory.
 */
template <class ProtocolImpl> class ProtocolFactoryBase : public NamedProtocolConfigFactory {
  ProtocolPtr createProtocol() override { return std::move(std::make_unique<ProtocolImpl>()); }

  std::string name() override { return name_; }

protected:
  ProtocolFactoryBase(const std::string& name) : name_(name) {}

private:
  const std::string name_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
