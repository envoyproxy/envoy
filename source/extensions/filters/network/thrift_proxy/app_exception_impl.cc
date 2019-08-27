#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

static const std::string TApplicationException = "TApplicationException";
static const std::string MessageField = "message";
static const std::string TypeField = "type";
static const std::string StopField = "";

DirectResponse::ResponseType AppException::encode(MessageMetadata& metadata,
                                                  ThriftProxy::Protocol& proto,
                                                  Buffer::Instance& buffer) const {
  // Handle cases where the exception occurs before the message name (e.g. some header transport
  // errors).
  if (!metadata.hasMethodName()) {
    metadata.setMethodName("");
  }
  if (!metadata.hasSequenceId()) {
    metadata.setSequenceId(0);
  }

  metadata.setMessageType(MessageType::Exception);

  proto.writeMessageBegin(buffer, metadata);
  proto.writeStructBegin(buffer, TApplicationException);

  proto.writeFieldBegin(buffer, MessageField, FieldType::String, 1);
  proto.writeString(buffer, std::string(what()));
  proto.writeFieldEnd(buffer);

  proto.writeFieldBegin(buffer, TypeField, FieldType::I32, 2);
  proto.writeInt32(buffer, static_cast<int32_t>(type_));
  proto.writeFieldEnd(buffer);

  proto.writeFieldBegin(buffer, StopField, FieldType::Stop, 0);

  proto.writeStructEnd(buffer);
  proto.writeMessageEnd(buffer);

  return DirectResponse::ResponseType::Exception;
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
