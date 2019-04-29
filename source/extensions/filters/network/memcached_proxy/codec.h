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
    OP_INCREMENT = 0x05,
    OP_INCREMENTQ = 0x15,
    OP_DECREMENT = 0x06,
    OP_DECREMENTQ = 0x16,
    OP_APPEND = 0x0e,
    OP_APPENDQ = 0x19,
    OP_PREPEND = 0x0f,
    OP_PREPENDQ = 0x1a,
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
 * Base class for all counter like requests (INCREMENT, DECREMENT)
 */
class CounterLikeRequest : public virtual Request {
public:
  virtual ~CounterLikeRequest() = default;
  virtual bool quiet() const PURE;
  virtual const std::string& key() const PURE;
  virtual uint64_t amount() const PURE;
  virtual uint64_t initialValue() const PURE;
  virtual uint32_t expiration() const PURE;
};

/**
 * Memcached OP_INCREMENT message.
 */
class IncrementRequest : public virtual CounterLikeRequest {
public:
  virtual ~IncrementRequest() = default;
  virtual bool operator==(const IncrementRequest& rhs) const PURE;
};

typedef std::unique_ptr<IncrementRequest> IncrementRequestPtr;

/**
 * Memcached OP_DECREMENT message.
 */
class DecrementRequest : public virtual CounterLikeRequest {
public:
  virtual ~DecrementRequest() = default;
  virtual bool operator==(const DecrementRequest& rhs) const PURE;
};

typedef std::unique_ptr<DecrementRequest> DecrementRequestPtr;

/**
 * Base class for all append like requests (APPEND, PREPEND)
 */
class AppendLikeRequest : public virtual Request {
public:
  virtual ~AppendLikeRequest() = default;
  virtual bool quiet() const PURE;
  virtual const std::string& key() const PURE;
  virtual const std::string& body() const PURE;
};

/**
 * Memcached OP_APPEND message.
 */
class AppendRequest : public virtual AppendLikeRequest {
public:
  virtual ~AppendRequest() = default;
  virtual bool operator==(const AppendRequest& rhs) const PURE;
};

typedef std::unique_ptr<AppendRequest> AppendRequestPtr;

/**
 * Memcached OP_PREPEND message.
 */
class PrependRequest : public virtual AppendLikeRequest {
public:
  virtual ~PrependRequest() = default;
  virtual bool operator==(const PrependRequest& rhs) const PURE;
};

typedef std::unique_ptr<PrependRequest> PrependRequestPtr;

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
  virtual void decodeIncrement(IncrementRequestPtr&& message) PURE;
  virtual void decodeDecrement(DecrementRequestPtr&& message) PURE;
  virtual void decodeAppend(AppendRequestPtr&& message) PURE;
  virtual void decodePrepend(PrependRequestPtr&& message) PURE;
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
 * A factory for a memcached decoder.
 */
class DecoderFactory {
public:
  virtual ~DecoderFactory() {}

  /**
   * Create a decoder given a set of decoder callbacks.
   */
  virtual DecoderPtr create(DecoderCallbacks& callbacks) PURE;
};

/**
 * Memcached message encoder.
 */
class Encoder {
public:
  virtual ~Encoder() = default;

  virtual void encodeGet(const GetRequest& request, Buffer::Instance& out) PURE;
  virtual void encodeGetk(const GetkRequest& request, Buffer::Instance& out) PURE;
  virtual void encodeDelete(const DeleteRequest& request, Buffer::Instance& out) PURE;
  virtual void encodeSet(const SetRequest& request, Buffer::Instance& out) PURE;
  virtual void encodeAdd(const AddRequest& request, Buffer::Instance& out) PURE;
  virtual void encodeReplace(const ReplaceRequest& request, Buffer::Instance& out) PURE;
  virtual void encodeIncrement(const IncrementRequest& request, Buffer::Instance& out) PURE;
  virtual void encodeDecrement(const DecrementRequest& request, Buffer::Instance& out) PURE;
  virtual void encodeAppend(const AppendRequest& request, Buffer::Instance& out) PURE;
  virtual void encodePrepend(const PrependRequest& request, Buffer::Instance& out) PURE;
};

typedef std::unique_ptr<Encoder> EncoderPtr;

} // MemcachedProxy
} // NetworkFilters
} // Extensions
} // Envoy
