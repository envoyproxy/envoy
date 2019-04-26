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
    OP_GETQ = 0x09,
    OP_GETK = 0x0c,
    OP_GETKQ = 0x0d,
    OP_DELETE = 0x04,
    OP_DELETEQ = 0x14,
    OP_SET = 0x01,
    OP_SETQ = 0x11,
    OP_ADD = 0x02,
    OP_ADDQ = 0x12,
    OP_REPLACE = 0x03,
    OP_REPLACEQ = 0x13,
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
};

typedef std::unique_ptr<Request> RequestPtr;

/**
 * Base class for all get like requests (GET, GETK)
 */
class GetLikeRequest : public virtual Request {
public:
  virtual ~GetLikeRequest() = default;

  virtual bool quiet() const PURE;
  virtual const std::string& key() const PURE;
};

/**
 * Memcached OP_GET message.
 */
class GetRequest : public virtual GetLikeRequest {
public:
  virtual ~GetRequest() = default;
  virtual bool operator==(const GetRequest& rhs) const PURE;
};

typedef std::unique_ptr<GetRequest> GetRequestPtr;

/**
 * Memcached OP_GETK message.
 */
class GetkRequest : public virtual GetLikeRequest {
public:
  virtual ~GetkRequest() = default;
  virtual bool operator==(const GetkRequest& rhs) const PURE;
};

typedef std::unique_ptr<GetkRequest> GetkRequestPtr;

/**
 * Memcached OP_DELETE message.
 */
class DeleteRequest : public virtual GetLikeRequest {
public:
  virtual ~DeleteRequest() = default;
  virtual bool operator==(const DeleteRequest& rhs) const PURE;
};

typedef std::unique_ptr<DeleteRequest> DeleteRequestPtr;

/**
 * Base class for all set like requests (SET, ADD, REPLACE)
 */
class SetLikeRequest : public virtual Request {
public:
  virtual ~SetLikeRequest() = default;
  virtual bool quiet() const PURE;
  virtual const std::string& key() const PURE;
  virtual const std::string& body() const PURE;
  virtual uint32_t flags() const PURE;
  virtual uint32_t expiration() const PURE;
};

/**
 * Memcached OP_SET message.
 */
class SetRequest : public virtual SetLikeRequest {
public:
  virtual ~SetRequest() = default;
  virtual bool operator==(const SetRequest& rhs) const PURE;
};

typedef std::unique_ptr<SetRequest> SetRequestPtr;

/**
 * Memcached OP_ADD message.
 */
class AddRequest : public virtual SetLikeRequest {
public:
  virtual ~AddRequest() = default;
  virtual bool operator==(const AddRequest& rhs) const PURE;
};

typedef std::unique_ptr<AddRequest> AddRequestPtr;

/**
 * Memcached OP_REPLACE message.
 */
class ReplaceRequest : public virtual SetLikeRequest {
public:
  virtual ~ReplaceRequest() = default;
  virtual bool operator==(const ReplaceRequest& rhs) const PURE;
};

typedef std::unique_ptr<ReplaceRequest> ReplaceRequestPtr;

/**
 * General callbacks for dispatching decoded memcached messages to a sink.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  virtual void decodeGet(GetRequestPtr&& message) PURE;
  virtual void decodeGetk(GetkRequestPtr&& message) PURE;
  virtual void decodeDelete(DeleteRequestPtr&& message) PURE;
  virtual void decodeSet(SetRequestPtr&& message) PURE;
  virtual void decodeAdd(AddRequestPtr&& message) PURE;
  virtual void decodeReplace(ReplaceRequestPtr&& message) PURE;
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
  virtual void encodeGetk(const GetkRequest& message) PURE;
  virtual void encodeDelete(const DeleteRequest& message) PURE;
  virtual void encodeSet(const SetRequest& message) PURE;
  virtual void encodeAdd(const AddRequest& message) PURE;
  virtual void encodeReplace(const ReplaceRequest& message) PURE;
};

} // MemcachedProxy
} // NetworkFilters
} // Extensions
} // Envoy
