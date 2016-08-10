#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

namespace Http {

/**
 * Wraps an HTTP message including its headers, body, and any trailers.
 */
class Message {
public:
  virtual ~Message() {}

  /**
   * @return HeaderMap& the message headers.
   */
  virtual HeaderMap& headers() PURE;

  /**
   * @return Buffer::Instance* the message body, if any.
   */
  virtual Buffer::Instance* body() PURE;

  /**
   * Set the body.
   * @param body supplies the new body.
   */
  virtual void body(Buffer::InstancePtr&& body) PURE;

  /**
   * @return HeaderMap* the message trailers, if any.
   */
  virtual HeaderMap* trailers() PURE;

  /**
   * Set the trailers.
   * @param trailers supplies the new trailers.
   */
  virtual void trailers(HeaderMapPtr&& trailers) PURE;

  /**
   * @return std::string the message body as a std::string.
   */
  virtual std::string bodyAsString() PURE;
};

typedef std::unique_ptr<Message> MessagePtr;

} // Http
