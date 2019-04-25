#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MemcachedProxy {

class Message {
public:
  enum class OpCode {
    OP_GET = 0x00,
    OP_SET = 0x01,
    OP_GETQ = 0x09,
    OP_SETQ = 0x11,
    OP_GETK = 0x0c,
    OP_GETKQ = 0x0d,

  };

  // Define some constants used in memcached messages encoding
  constexpr static uint8_t RequestV1 = 0x80;
  constexpr static uint8_t ResponseV1 = 0x81;
  constexpr static uint8_t HeaderSize = 24;
};

/*
 * Base class for all memcached messages
 */
class Request {
public:
  virtual ~Request() = default;

  virtual uint8_t dataType() const PURE;
  virtual uint8_t vbucketIdOrStatus() const PURE;
  virtual uint32_t opaque() const PURE;
  virtual uint64_t cas() const PURE;
  virtual Message::OpCode opCode() const PURE;
};

typedef std::unique_ptr<Request> RequestPtr;

/**
 * Memcached OP_GET message.
 */
class GetRequest : public virtual Request {
public:
  virtual ~GetRequest() = default;

  virtual bool operator==(const GetRequest& rhs) const PURE;
  virtual bool quiet() const PURE;
  virtual const std::string& key() const PURE;
};

typedef std::unique_ptr<GetRequest> GetRequestPtr;

/**
 * Memcached OP_SET message.
 */
class SetRequest : public virtual Request {
public:
  virtual ~SetRequest() = default;

  virtual bool operator==(const SetRequest& rhs) const PURE;
  virtual bool quiet() const PURE;
  virtual const std::string& key() const PURE;
  virtual const std::string& body() const PURE;
  virtual uint32_t flags() const PURE;
  virtual uint32_t expiration() const PURE;
};

typedef std::unique_ptr<SetRequest> SetRequestPtr;

/**
 * General callbacks for dispatching decoded memcached messages to a sink.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  virtual void decodeGet(GetRequestPtr&& message) PURE;
  virtual void decodeSet(SetRequestPtr&& message) PURE;
};

/**
 * Memcached message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() = default;

  virtual void onData(Buffer::Instance& data) PURE;
};

typedef std::unique_ptr<Decoder> DecoderPtr;

/**
 * Memcached message encoder.
 */
class Encoder {
public:
  virtual ~Encoder() = default;

  virtual void encodeGet(const GetRequest& message) PURE;
  virtual void encodeSet(const SetRequest& message) PURE;
};

} // MemcachedProxy
} // NetworkFilters
} // Extensions
} // Envoy
