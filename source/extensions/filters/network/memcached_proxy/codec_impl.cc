#include "extensions/filters/network/memcached_proxy/codec_impl.h"

#include <cstdint>
#include <list>
#include <memory>
#include <sstream>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MemcachedProxy {

void GetRequestImpl::fromBuffer(Buffer::Instance&) {
  ENVOY_LOG(trace, "decoding get request");
  // key_ = BufferHelper::removeCString(data);
}

void SetRequestImpl::fromBuffer(Buffer::Instance&) {
  ENVOY_LOG(trace, "decoding set request");
  // key_ = BufferHelper::removeCString(data);
  // flags_ = BufferHelper::removeInt32(data);
  // expiration_ = BufferHelper::removeInt32(data);
  // body_ = BufferHelper::removeCString(data);
}

bool DecoderImpl::decodeRequest(Buffer::Instance& data) {
  Message::Header header{
    .magic = data.drainBEInt<uint8_t>(),
    .op_code = data.drainBEInt<uint8_t>(),
    .key_length = data.drainBEInt<uint16_t>(),
    .extras_length = data.drainBEInt<uint8_t>(),
    .data_type = data.drainBEInt<uint8_t>(),
    .vbucket_id_or_status = data.drainBEInt<uint8_t>(),
    .body_length = data.drainBEInt<uint32_t>(),
    .opaque = data.drainBEInt<uint32_t>(),
    .cas = data.drainBEInt<uint64_t>(),
  };


  Message::OpCode op_code = static_cast<Message::OpCode>(header.op_code);

  switch (op_code) {
  case Message::OpCode::OP_GET:
  case Message::OpCode::OP_GETQ: {
    auto message = std::make_unique<GetRequestImpl>(std::move(header));
    message->fromBuffer(data);
    callbacks_.decodeGet(std::move(message));
    break;
  }

  case Message::OpCode::OP_SET:
  case Message::OpCode::OP_SETQ: {
    auto message = std::make_unique<SetRequestImpl>(std::move(header));
    message->fromBuffer(data);
    callbacks_.decodeSet(std::move(message));
    break;
  }

  default:
    throw EnvoyException(fmt::format("invalid memcached op {}", header.op_code));
  }

  return true;
}

bool DecoderImpl::decode(Buffer::Instance& data) {
  ENVOY_LOG(trace, "decoding {} bytes", data.length());
  if (data.length() < Message::HeaderSize) {
    return false;
  }

  auto magic = data.peekBEInt<uint32_t>();
  switch (magic) {
  case Message::RequestV1: {
    return decodeRequest(data);
  }
  case Message::ResponseV1: {
    return decodeResponse(data);
  }
  default:
    throw EnvoyException(fmt::format("invalid memcached message type {}", magic));
  }

  ENVOY_LOG(trace, "{} bytes remaining after decoding", data.length());
  return true;
}

void DecoderImpl::onData(Buffer::Instance& data) {
  while (data.length() > 0 && decode(data)) {}
}

void EncoderImpl::encodeRequestHeader(
  uint32_t key_length,
  uint32_t body_length,
  uint32_t extras_length,
  const Request& request,
  Message::OpCode op_code) {

  output_.writeByte(Message::RequestV1);
  output_.writeByte(op_code);
  output_.writeBEInt<uint16_t>(key_length);
  output_.writeByte(extras_length);
  output_.writeByte(Message::RawDataType);
  output_.writeByte(request.vbucketIdOrStatus());
  output_.writeBEInt<uint32_t>(body_length);
  output_.writeBEInt<uint32_t>(request.opaque());
  output_.writeBEInt<uint32_t>(request.cas());
}

void EncoderImpl::encodeGet(const GetRequest& request) {
  uint32_t key_length = 0; // todo
  uint32_t body_length = 0; // todo
  uint32_t extras_length = 0; // todo

  encodeRequestHeader(key_length, body_length, extras_length, request,
    request.quiet() ? Message::OpCode::OP_GETQ : Message::OpCode::OP_GET);

  output_.add(request.key());
}

void EncoderImpl::encodeSet(const SetRequest& request) {
  uint32_t key_length = 0; // todo
  uint32_t body_length = 0; // todo
  uint32_t extras_length = 0; // todo

  encodeRequestHeader(key_length, body_length, extras_length, request,
    request.quiet() ? Message::OpCode::OP_SETQ : Message::OpCode::OP_SET);

  output_.writeBEInt<uint32_t>(request.flags());
  output_.writeBEInt<uint32_t>(request.expiration());
  output_.add(request.key());
  output_.add(request.body());
}

}
}
}
}
