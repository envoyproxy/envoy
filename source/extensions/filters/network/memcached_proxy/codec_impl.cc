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
  std::string ret(start, length);
  data.drain(length);
  return ret;
}

void GetLikeRequestImpl::fromBuffer(uint16_t key_length, uint8_t, uint32_t, Buffer::Instance& data) {
  key_ = BufferHelper::drainString(data, key_length);
}

bool GetLikeRequestImpl::equals(const GetLikeRequest& rhs) const {
  return dataType() == rhs.dataType() &&
    vbucketIdOrStatus() == rhs.vbucketIdOrStatus() &&
    opaque() == rhs.opaque() &&
    cas() == rhs.cas() &&
    key() == rhs.key();
}

void SetLikeRequestImpl::fromBuffer(uint16_t key_length, uint8_t, uint32_t body_length, Buffer::Instance& data) {
  flags_ = data.drainBEInt<uint32_t>();
  expiration_ = data.drainBEInt<uint32_t>();
  key_ = BufferHelper::drainString(data, key_length);
  body_ = BufferHelper::drainString(data, body_length);
}

bool SetLikeRequestImpl::equals(const SetLikeRequest& rhs) const {
  return dataType() == rhs.dataType() &&
    vbucketIdOrStatus() == rhs.vbucketIdOrStatus() &&
    opaque() == rhs.opaque() &&
    cas() == rhs.cas() &&
    key() == rhs.key() &&
    body() == rhs.body() &&
    expiration() == rhs.expiration() &&
    flags() == rhs.flags();
}

bool DecoderImpl::decodeRequest(Buffer::Instance& data) {
  auto op_code = static_cast<Message::OpCode>(data.drainBEInt<uint8_t>());
  auto key_length = data.drainBEInt<uint16_t>();
  auto extras_length = data.drainBEInt<uint8_t>();
  auto data_type = data.drainBEInt<uint8_t>();
  auto vbucket_id_or_status = data.drainBEInt<uint16_t>();
  auto body_length = data.drainBEInt<uint32_t>();
  auto opaque = data.drainBEInt<uint32_t>();
  auto cas = data.drainBEInt<uint64_t>();

  // TODO: quiet flag.
  switch (op_code) {
  case Message::OpCode::OP_GET:
  case Message::OpCode::OP_GETQ: {
    auto message = std::make_unique<GetRequestImpl>(data_type, vbucket_id_or_status, opaque, cas);
    message->fromBuffer(key_length, extras_length, body_length, data);
    ENVOY_LOG(trace, "decoded `GET` key={}", message->key());
    callbacks_.decodeGet(std::move(message));
    break;
  }
  case Message::OpCode::OP_GETK:
  case Message::OpCode::OP_GETKQ: {
    auto message = std::make_unique<GetkRequestImpl>(data_type, vbucket_id_or_status, opaque, cas);
    message->fromBuffer(key_length, extras_length, body_length, data);
    ENVOY_LOG(trace, "decoded `GETK` key={}", message->key());
    callbacks_.decodeGetk(std::move(message));
    break;
  }

  case Message::OpCode::OP_SET:
  case Message::OpCode::OP_SETQ: {
    auto message = std::make_unique<SetRequestImpl>(data_type, vbucket_id_or_status, opaque, cas);
    message->fromBuffer(key_length, extras_length, body_length, data);
    ENVOY_LOG(trace, "decoded `SET` key={}, body={}", message->key(), message->body());
    callbacks_.decodeSet(std::move(message));
    break;
  }

  case Message::OpCode::OP_ADD:
  case Message::OpCode::OP_ADDQ: {
    auto message = std::make_unique<AddRequestImpl>(data_type, vbucket_id_or_status, opaque, cas);
    message->fromBuffer(key_length, extras_length, body_length, data);
    ENVOY_LOG(trace, "decoded `ADD` key={}, body={}", message->key(), message->body());
    callbacks_.decodeAdd(std::move(message));
    break;
  }

  case Message::OpCode::OP_REPLACE:
  case Message::OpCode::OP_REPLACEQ: {
    auto message = std::make_unique<ReplaceRequestImpl>(data_type, vbucket_id_or_status, opaque, cas);
    message->fromBuffer(key_length, extras_length, body_length, data);
    ENVOY_LOG(trace, "decoded `REPLACE` key={}, body={}", message->key(), message->body());
    callbacks_.decodeReplace(std::move(message));
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

  auto magic = data.drainBEInt<uint8_t>();
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
  uint16_t key_length,
  uint8_t extras_length,
  uint32_t body_length,
  const Request& request,
  Message::OpCode op_code) {

  output_.writeByte(Message::RequestV1);
  output_.writeByte(op_code);
  output_.writeBEInt<uint16_t>(key_length);
  output_.writeByte(extras_length);
  output_.writeByte(request.dataType());
  output_.writeBEInt<uint16_t>(request.vbucketIdOrStatus());
  output_.writeBEInt<uint32_t>(body_length);
  output_.writeBEInt<uint32_t>(request.opaque());
  output_.writeBEInt<uint64_t>(request.cas());
}

void EncoderImpl::encodeGet(const GetRequest& request) {
  encodeGetLike(request, request.quiet() ? Message::OpCode::OP_GETQ : Message::OpCode::OP_GET);
}

void EncoderImpl::encodeGetk(const GetkRequest& request) {
  encodeGetLike(request, request.quiet() ? Message::OpCode::OP_GETKQ : Message::OpCode::OP_GETK);
}

void EncoderImpl::encodeGetLike(const GetLikeRequest& request, Message::OpCode op_code) {
  encodeRequestHeader(request.key().length(), 0, 0, request, op_code);
  output_.add(request.key());
}

void EncoderImpl::encodeSet(const SetRequest& request) {
  encodeSetLike(request, request.quiet() ? Message::OpCode::OP_SETQ : Message::OpCode::OP_SET);
}

void EncoderImpl::encodeAdd(const AddRequest& request) {
  encodeSetLike(request, request.quiet() ? Message::OpCode::OP_ADDQ : Message::OpCode::OP_ADD);
}

void EncoderImpl::encodeReplace(const ReplaceRequest& request) {
  encodeSetLike(request, request.quiet() ? Message::OpCode::OP_REPLACEQ : Message::OpCode::OP_REPLACE);
}

void EncoderImpl::encodeSetLike(const SetLikeRequest& request, Message::OpCode op_code) {
  encodeRequestHeader(request.key().length(), 8, request.body().length(), request, op_code);
  output_.writeBEInt<uint32_t>(request.flags());
  output_.writeBEInt<uint32_t>(request.expiration());
  output_.add(request.key());
  output_.add(request.body());
}

}
}
}
}
