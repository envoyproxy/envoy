#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

static const std::string TApplicationException = "TApplicationException";
static const std::string MessageField = "message";
static const std::string TypeField = "type";
static const std::string StopField = "";

void AppException::encode(ThriftProxy::Protocol& proto, Buffer::Instance& buffer) {
  proto.writeMessageBegin(buffer, method_name_, ThriftProxy::MessageType::Exception, seq_id_);
  proto.writeStructBegin(buffer, TApplicationException);

  proto.writeFieldBegin(buffer, MessageField, ThriftProxy::FieldType::String, 1);
  proto.writeString(buffer, error_message_);
  proto.writeFieldEnd(buffer);

  proto.writeFieldBegin(buffer, TypeField, ThriftProxy::FieldType::I32, 2);
  proto.writeInt32(buffer, static_cast<int32_t>(type_));
  proto.writeFieldEnd(buffer);

  proto.writeFieldBegin(buffer, StopField, ThriftProxy::FieldType::Stop, 0);

  proto.writeStructEnd(buffer);
  proto.writeMessageEnd(buffer);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
