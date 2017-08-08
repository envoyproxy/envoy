#pragma once

#include <string>

#include "envoy/http/header_map.h"
#include "envoy/http/message.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/non_copyable.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Http {

/**
 * Implementation of Http::Message. This implementation does not support streaming.
 */
class MessageImpl : public Http::Message {
public:
  // Http::Message
  HeaderMap& headers() override { return *headers_; }
  Buffer::InstancePtr& body() override { return body_; }
  HeaderMap* trailers() override { return trailers_.get(); }
  void trailers(HeaderMapPtr&& trailers) override { trailers_ = std::move(trailers); }
  std::string bodyAsString() const override;

protected:
  MessageImpl(HeaderMapPtr&& headers) : headers_(std::move(headers)) {}

private:
  HeaderMapPtr headers_;
  Buffer::InstancePtr body_;
  HeaderMapPtr trailers_;
};

class RequestMessageImpl : public MessageImpl {
public:
  RequestMessageImpl() : MessageImpl(HeaderMapPtr{new HeaderMapImpl()}) {}
  RequestMessageImpl(HeaderMapPtr&& headers) : MessageImpl(std::move(headers)) {}
};

class ResponseMessageImpl : public MessageImpl {
public:
  ResponseMessageImpl(HeaderMapPtr&& headers) : MessageImpl(std::move(headers)) {}
};

} // namespace Http
} // namespace Envoy
