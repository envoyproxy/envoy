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

std::string BufferHelper::drainString(Buffer::Instance& data, uint32_t length) {
  char* start = reinterpret_cast<char*>(data.linearize(length));
  std::string ret(start);
  data.drain(length);
  return ret;
}

void GetRequestImpl::fromBuffer(uint16_t key_length, uint8_t, uint32_t, Buffer::Instance& data) {
  ENVOY_LOG(trace, "decoding get request");
  key_ = BufferHelper::drainString(data, key_length);
}

void SetRequestImpl::fromBuffer(uint16_t key_length, uint8_t, uint32_t body_length, Buffer::Instance& data) {
  ENVOY_LOG(trace, "decoding set request");
  key_ = BufferHelper::drainString(data, key_length);
  flags_ = data.drainBEInt<uint32_t>();
  expiration_ = data.drainBEInt<uint32_t>();
  body_ = BufferHelper::drainString(data, body_length);
}

bool DecoderImpl::decodeRequest(Buffer::Instance& data) {
  data.drainBEInt<uint8_t>(); // skip magic byte as we've already peeked it.
  auto op_code = static_cast<Message::OpCode>(data.drainBEInt<uint8_t>());
  auto key_length = data.drainBEInt<uint16_t>();
  auto extras_length = data.drainBEInt<uint8_t>();
  data.drainBEInt<uint8_t>(); // skip data type as it's always 0x00.
  auto vbucket_id_or_status = data.drainBEInt<uint8_t>();
  auto body_length = data.drainBEInt<uint32_t>();
  auto opaque = data.drainBEInt<uint32_t>();
  auto cas = data.drainBEInt<uint64_t>();

  switch (op_code) {
  case Message::OpCode::OP_GET:
  case Message::OpCode::OP_GETQ: {
    auto message = std::make_unique<GetRequestImpl>(vbucket_id_or_status, opaque, cas);
    message->fromBuffer(key_length, extras_length, body_length, data);
    callbacks_.decodeGet(std::move(message));
    break;
  }

  case Message::OpCode::OP_SET:
  case Message::OpCode::OP_SETQ: {
    auto message = std::make_unique<SetRequestImpl>(vbucket_id_or_status, opaque, cas);
    message->fromBuffer(key_length, extras_length, body_length, data);
    callbacks_.decodeSet(std::move(message));
    break;
  }

  default:
    throw EnvoyException(fmt::format("invalid memcached op {}", static_cast<uint8_t>(op_code)));
  }

  return true;
}

bool DecoderImpl::decodeResponse(Buffer::Instance&) {
  // TODO: ???
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
  encodeRequestHeader(
    request.key().length(),
    0,
    0,
    request,
    request.quiet() ? Message::OpCode::OP_GETQ : Message::OpCode::OP_GET);

  output_.add(request.key());
}

void EncoderImpl::encodeSet(const SetRequest& request) {
  encodeRequestHeader(
    request.key().length(),
    request.body().length(),
    64,
    request,
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
