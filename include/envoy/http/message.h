#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

/**
 * Wraps an HTTP message including its headers, body, and any trailers.
 */
template <class HeaderType, class TrailerType> class Message {
public:
  virtual ~Message() = default;

  /**
   * @return HeaderType& the message headers.
   */
  virtual HeaderType& headers() PURE;

  /**
   * @return Buffer::InstancePtr& the message body, if any. Callers are free to reallocate, remove,
   *         etc. the body.
   */
  virtual Buffer::InstancePtr& body() PURE;

  /**
   * @return TrailerType* the message trailers, if any.
   */
  virtual TrailerType* trailers() PURE;

  /**
   * Set the trailers.
   * @param trailers supplies the new trailers.
   */
  virtual void trailers(std::unique_ptr<TrailerType>&& trailers) PURE;

  /**
   * @return std::string the message body as a std::string.
   */
  virtual std::string bodyAsString() const PURE;
};

using RequestMessage = Message<RequestHeaderMap, RequestTrailerMap>;
using RequestMessagePtr = std::unique_ptr<RequestMessage>;
using ResponseMessage = Message<ResponseHeaderMap, ResponseTrailerMap>;
using ResponseMessagePtr = std::unique_ptr<ResponseMessage>;

} // namespace Http
} // namespace Envoy
