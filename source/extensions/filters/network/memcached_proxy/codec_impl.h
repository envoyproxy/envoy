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

class BufferHelper {
public:
  static std::string drainString(Buffer::Instance& buffer, uint32_t size);
};

class RequestImpl : public virtual Request {
public:
  RequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    data_type_(data_type), vbucket_id_or_status_(vbucket_id_or_status), opaque_(opaque), cas_(cas) {}

  virtual void fromBuffer(uint16_t key_length, uint8_t extras_length, uint32_t body_length, Buffer::Instance& data) PURE;

  uint8_t dataType() const override { return data_type_; }
  uint8_t vbucketIdOrStatus() const override { return vbucket_id_or_status_; }
  uint32_t opaque() const override { return opaque_; }
  uint64_t cas() const override { return cas_; }
private:
  const uint8_t data_type_;
  const uint8_t vbucket_id_or_status_;
  const uint32_t opaque_;
  const uint64_t cas_;
};

class GetLikeRequestImpl : public RequestImpl,
                           public virtual GetLikeRequest {
public:
  GetLikeRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) : RequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // RequestImpl
  void fromBuffer(uint16_t key_length, uint8_t extras_length, uint32_t body_length, Buffer::Instance& data) override;

  // GetLikeRequest
  bool quiet() const override { return false; }
  const std::string& key() const override { return key_; }

  void key(const std::string& key) { key_ = key; }
protected:
  bool equals(const GetLikeRequest& rhs) const;
private:
  std::string key_;
};

class GetRequestImpl : public GetLikeRequestImpl,
                       public GetRequest {
public:
  GetRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    GetLikeRequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // GetRequest
  bool operator==(const GetRequest& rhs) const override { return equals(rhs); }
};

class GetkRequestImpl : public GetLikeRequestImpl,
                        public GetkRequest {
public:
  GetkRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    GetLikeRequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // GetkRequest
  bool operator==(const GetkRequest& rhs) const override { return equals(rhs); }
};

class DeleteRequestImpl : public GetLikeRequestImpl,
                          public DeleteRequest {
public:
  DeleteRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    GetLikeRequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // DeleteRequest
  bool operator==(const DeleteRequest& rhs) const override { return equals(rhs); }
};

class SetLikeRequestImpl : public RequestImpl,
                           public virtual SetLikeRequest {
public:
  SetLikeRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) : RequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // RequestImpl
  void fromBuffer(uint16_t key_length, uint8_t extras_length, uint32_t body_length, Buffer::Instance& data) override;

  // SetLikeRequest
  bool quiet() const override { return false; }
  const std::string& key() const override { return key_; }
  const std::string& body() const override { return body_; }
  uint32_t flags() const override { return flags_; }
  uint32_t expiration() const override { return expiration_; }

  void key(const std::string& key) { key_ = key; }
  void body(const std::string& body) { body_ = body; }
  void flags(uint32_t flags) { flags_ = flags; }
  void expiration(uint32_t expiration) { expiration_ = expiration; }
protected:
  bool equals(const SetLikeRequest& rhs) const;
private:
  std::string key_;
  std::string body_;
  uint32_t flags_;
  uint32_t expiration_;
};

class SetRequestImpl : public SetLikeRequestImpl,
                       public SetRequest {
public:
  SetRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    SetLikeRequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // SetRequest
  bool operator==(const SetRequest& rhs) const override { return equals(rhs); }
};

class AddRequestImpl : public SetLikeRequestImpl,
                       public AddRequest {
public:
  AddRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    SetLikeRequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // AddRequest
  bool operator==(const AddRequest& rhs) const override { return equals(rhs); }
};

class ReplaceRequestImpl : public SetLikeRequestImpl,
                       public ReplaceRequest {
public:
  ReplaceRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    SetLikeRequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // ReplaceRequest
  bool operator==(const ReplaceRequest& rhs) const override { return equals(rhs); }
};

class CounterLikeRequestImpl : public RequestImpl,
                               public virtual CounterLikeRequest {
public:
  CounterLikeRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) : RequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // RequestImpl
  void fromBuffer(uint16_t key_length, uint8_t extras_length, uint32_t body_length, Buffer::Instance& data) override;

  // CounterLikeRequest
  bool quiet() const override { return false; }
  const std::string& key() const override { return key_; }
  uint64_t amount() const override { return amount_; }
  uint64_t initialValue() const override { return initial_value_; }
  uint32_t expiration() const override { return expiration_; }

  void key(const std::string& key) { key_ = key; }
  void amount(uint64_t amount) { amount_ = amount; }
  void initialValue(uint64_t initial_value) { initial_value_ = initial_value; }
  void expiration(uint32_t expiration) { expiration_ = expiration; }
protected:
  bool equals(const CounterLikeRequest& rhs) const;
private:
  std::string key_;
  uint64_t amount_;
  uint64_t initial_value_;
  uint32_t expiration_;
};

class IncrementRequestImpl : public CounterLikeRequestImpl,
                             public IncrementRequest {
public:
  IncrementRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    CounterLikeRequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // IncrementRequest
  bool operator==(const IncrementRequest& rhs) const override { return equals(rhs); }
};

class DecrementRequestImpl : public CounterLikeRequestImpl,
                             public DecrementRequest {
public:
  DecrementRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    CounterLikeRequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // DecrementRequest
  bool operator==(const DecrementRequest& rhs) const override { return equals(rhs); }
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

class DecoderFactoryImpl : public DecoderFactory {
public:
  // DecoderFactory
  DecoderPtr create(DecoderCallbacks& callbacks) override {
    return std::make_unique<DecoderImpl>(callbacks);
  }
};

class AppendLikeRequestImpl : public RequestImpl,
                              public virtual AppendLikeRequest {
public:
  AppendLikeRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) : RequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // RequestImpl
  void fromBuffer(uint16_t key_length, uint8_t extras_length, uint32_t body_length, Buffer::Instance& data) override;

  // AppendLikeRequest
  bool quiet() const override { return false; }
  const std::string& key() const override { return key_; }
  const std::string& body() const override { return body_; }

  void key(const std::string& key) { key_ = key; }
  void body(const std::string& body) { body_ = body; }
protected:
  bool equals(const AppendLikeRequest& rhs) const;
private:
  std::string key_;
  std::string body_;
};

class AppendRequestImpl : public AppendLikeRequestImpl,
                          public AppendRequest {
public:
  AppendRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    AppendLikeRequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // AppendRequest
  bool operator==(const AppendRequest& rhs) const override { return equals(rhs); }
};

class PrependRequestImpl : public AppendLikeRequestImpl,
                           public PrependRequest {
public:
  PrependRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    AppendLikeRequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // PrependRequest
  bool operator==(const PrependRequest& rhs) const override { return equals(rhs); }
};

class EncoderImpl : public Encoder, Logger::Loggable<Logger::Id::memcached> {
public:
  EncoderImpl() {}

  // Memcached::Encoder
  void encodeGet(const GetRequest& request, Buffer::Instance& out) override;
  void encodeGetk(const GetkRequest& request, Buffer::Instance& out) override;
  void encodeDelete(const DeleteRequest& request, Buffer::Instance& out) override;
  void encodeSet(const SetRequest& request, Buffer::Instance& out) override;
  void encodeAdd(const AddRequest& request, Buffer::Instance& out) override;
  void encodeReplace(const ReplaceRequest& request, Buffer::Instance& out) override;
  void encodeIncrement(const IncrementRequest& request, Buffer::Instance& out) override;
  void encodeDecrement(const DecrementRequest& request, Buffer::Instance& out) override;
  void encodeAppend(const AppendRequest& request, Buffer::Instance& out) override;
  void encodePrepend(const PrependRequest& request, Buffer::Instance& out) override;
private:
  void encodeGetLike(const GetLikeRequest& request, Message::OpCode op_code, Buffer::Instance& out);
  void encodeSetLike(const SetLikeRequest& request, Message::OpCode op_code, Buffer::Instance& out);
  void encodeCounterLike(const CounterLikeRequest& request, Message::OpCode op_code, Buffer::Instance& out);
  void encodeAppendLike(const AppendLikeRequest& request, Message::OpCode op_code, Buffer::Instance& out);
  void encodeRequestHeader(
    uint16_t key_length,
    uint8_t extras_length,
    uint32_t body_length,
    const Request& request,
    Message::OpCode op,
    Buffer::Instance& out);
};

}
}
}
}
