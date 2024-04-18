#pragma once

#include <string>

#include "envoy/http/header_map.h"
#include "envoy/http/message.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/non_copyable.h"
#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Http {

/**
 * Implementation of Http::Message. This implementation does not support streaming.
 */
template <class HeadersInterfaceType, class HeadersImplType, class TrailersInterfaceType,
          class TrailersImplType>
class MessageImpl : public Message<HeadersInterfaceType, TrailersInterfaceType> {
public:
  MessageImpl() : headers_(HeadersImplType::create()) {}
  MessageImpl(std::unique_ptr<HeadersInterfaceType>&& headers) : headers_(std::move(headers)) {}

  // Http::Message
  HeadersInterfaceType& headers() override { return *headers_; }
  Buffer::Instance& body() override { return body_; }
  TrailersInterfaceType* trailers() override { return trailers_.get(); }
  void trailers(std::unique_ptr<TrailersInterfaceType>&& trailers) override {
    trailers_ = std::move(trailers);
  }
  std::string bodyAsString() const override { return body_.toString(); }

private:
  std::unique_ptr<HeadersInterfaceType> headers_;
  Buffer::OwnedImpl body_;
  std::unique_ptr<TrailersInterfaceType> trailers_;
};

using RequestMessageImpl =
    MessageImpl<RequestHeaderMap, RequestHeaderMapImpl, RequestTrailerMap, RequestTrailerMapImpl>;
using ResponseMessageImpl = MessageImpl<ResponseHeaderMap, ResponseHeaderMapImpl,
                                        ResponseTrailerMap, ResponseTrailerMapImpl>;

} // namespace Http
} // namespace Envoy
