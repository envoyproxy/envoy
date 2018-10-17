#pragma once

#include "extensions/filters/network/dubbo_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class MessageImpl : public virtual Message {
public:
  MessageImpl(int64_t request_id, int32_t body_size, bool is_event)
      : request_id_(request_id), body_size_(body_size), is_event_(is_event) {}
  virtual ~MessageImpl() {}
  virtual int32_t bodySize() const override { return body_size_; }

  // Is a normal message or event
  virtual bool isEvent() const override { return is_event_; }

  virtual int64_t requestId() const override { return request_id_; }

  virtual std::string toString() const override {
    return fmt::format("body size:{}, is event:{}, request id: {}", body_size_, is_event_,
                       request_id_);
  }

protected:
  int64_t request_id_;
  int32_t body_size_;
  bool is_event_;
};

class RequestMessageImpl : public MessageImpl, public RequestMessage {
public:
  using MessageImpl::MessageImpl;

  virtual ~RequestMessageImpl() {}
  void fromBuffer(Buffer::Instance& data);
  virtual MessageType messageType() const override { return MessageType::Request; }

  virtual SerializationType serializationType() const override { return type_; }

  virtual bool isTwoWay() const override { return is_two_way_; }

private:
  SerializationType type_;
  bool is_two_way_;
};

typedef std::unique_ptr<RequestMessageImpl> RequestMessageImplPtr;

class ResponseMessageImpl : public MessageImpl, public ResponseMessage {
public:
  using MessageImpl::MessageImpl;

  virtual ~ResponseMessageImpl() {}
  void fromBuffer(Buffer::Instance& data);

  virtual MessageType messageType() const override { return MessageType::Response; }

  virtual ResponseStatus responseStatus() const override { return status_; }

private:
  ResponseStatus status_;
};

typedef std::unique_ptr<ResponseMessageImpl> ResponseMessageImplPtr;

class DubboProtocolImpl : public Protocol {
public:
  DubboProtocolImpl(ProtocolCallbacks& callbacks) : callbacks_(callbacks) {}
  const std::string& name() const override { return ProtocolNames::get().Dubbo; }
  virtual bool decode(Buffer::Instance& buffer, Protocol::Context* context) override;

  static constexpr uint8_t MessageSize = 16;
  static constexpr int32_t MaxBodySize = 16 * 1024 * 1024;

private:
  ProtocolCallbacks& callbacks_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
