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
template <class HeadersInterfaceType, class HeadersImplType, class TrailersInterfaceType,
          class TrailersImplType>
class MessageImpl : public Message<HeadersInterfaceType, TrailersInterfaceType> {
public:
  MessageImpl() : headers_(std::make_unique<HeadersImplType>()) {}
  MessageImpl(std::unique_ptr<HeadersInterfaceType>&& headers) : headers_(std::move(headers)) {}

  // Http::Message
  HeadersInterfaceType& headers() override { return *headers_; }
  Buffer::InstancePtr& body() override { return body_; }
  TrailersInterfaceType* trailers() override { return trailers_.get(); }
  void trailers(std::unique_ptr<TrailersInterfaceType>&& trailers) override {
    trailers_ = std::move(trailers);
  }
  std::string bodyAsString() const override {
    std::string ret;
    if (body_) {
      uint64_t num_slices = body_->getRawSlices(nullptr, 0);
      absl::FixedArray<Buffer::RawSlice> slices(num_slices);
      body_->getRawSlices(slices.begin(), num_slices);
      for (const Buffer::RawSlice& slice : slices) {
        ret.append(reinterpret_cast<const char*>(slice.mem_), slice.len_);
      }
    }
    return ret;
  }

private:
  std::unique_ptr<HeadersInterfaceType> headers_;
  Buffer::InstancePtr body_;
  std::unique_ptr<TrailersInterfaceType> trailers_;
};

using RequestMessageImpl =
    MessageImpl<RequestHeaderMap, RequestHeaderMapImpl, RequestTrailerMap, RequestTrailerMapImpl>;
using ResponseMessageImpl = MessageImpl<ResponseHeaderMap, ResponseHeaderMapImpl,
                                        ResponseTrailerMap, ResponseTrailerMapImpl>;

} // namespace Http
} // namespace Envoy
