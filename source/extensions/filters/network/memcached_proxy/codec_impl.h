#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "common/common/logger.h"
#include "common/buffer/buffer_impl.h"

#include "envoy/buffer/buffer.h"

#include "extensions/filters/network/memcached_proxy/codec.h"
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MemcachedProxy {

class RequestImpl : public virtual Request {
public:
  RequestImpl(Message::Header header) : header_(header) {}

  virtual void fromBuffer(Buffer::Instance& data) PURE;

  uint8_t vbucketIdOrStatus() const override { return header_.vbucket_id_or_status; }
  uint32_t opaque() const override { return header_.opaque; }
  uint64_t cas() const override { return header_.cas; }

private:
  Message::Header header_;
};

class GetRequestImpl : public RequestImpl,
                       public GetRequest,
                       Logger::Loggable<Logger::Id::memcached> {
public:
  GetRequestImpl(Message::Header header) : RequestImpl(header) {}

  // RequestImpl
  void fromBuffer(Buffer::Instance& data) override;

  bool quiet() const override { return false; }
  const std::string& key() const override { return key_; }
private:
  std::string key_;
};

class SetRequestImpl : public RequestImpl,
                       public SetRequest,
                       Logger::Loggable<Logger::Id::memcached> {
public:
  SetRequestImpl(Message::Header header) : RequestImpl(header) {}

  // RequestImpl
  void fromBuffer(Buffer::Instance& data) override;

  bool quiet() const override { return false; }
  const std::string& key() const override { return key_; }
  const std::string& body() const override { return body_; }
  uint32_t flags() const override { return flags_; }
  uint32_t expiration() const override { return expiration_; }
private:
  std::string key_;
  std::string body_;
  uint32_t flags_;
  uint32_t expiration_;
};

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::memcached> {
public:
  DecoderImpl(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  // Memcached::Decoder
  void onData(Buffer::Instance& data) override;

private:
  bool decode(Buffer::Instance& data);
  bool decodeRequest(Buffer::Instance& data);
  bool decodeResponse(Buffer::Instance& data);

  DecoderCallbacks& callbacks_;
};

class EncoderImpl : public Encoder, Logger::Loggable<Logger::Id::memcached> {
public:
  EncoderImpl(Buffer::Instance& output) : output_(output) {}

  // Memcached::Encoder
  void encodeGet(const GetRequest& message) override;
  void encodeSet(const SetRequest& message) override;

private:
  void encodeRequestHeader(
    uint32_t key_length,
    uint32_t body_length,
    uint32_t extras_length,
    const Message& message,
    Message::OpCode op);

  Buffer::Instance& output_;
};

}
}
}
}
