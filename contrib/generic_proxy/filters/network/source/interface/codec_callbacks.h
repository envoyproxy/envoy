
#pragma once

#include "envoy/buffer/buffer.h"

#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Decoder callback of request.
 */
class RequestDecoderCallback {
public:
  virtual ~RequestDecoderCallback() = default;

  /**
   * If request decoding success then this method will be called.
   * @param request request from decoding.
   */
  virtual void onDecodingSuccess(RequestPtr request) PURE;

  /**
   * If request decoding failure then this method will be called.
   */
  virtual void onDecodingFailure() PURE;
};

/**
 * Decoder callback of Response.
 */
class ResponseDecoderCallback {
public:
  virtual ~ResponseDecoderCallback() = default;

  /**
   * If response decoding success then this method will be called.
   * @param response response from decoding.
   */
  virtual void onDecodingSuccess(ResponsePtr response) PURE;

  /**
   * If response decoding failure then this method will be called.
   */
  virtual void onDecodingFailure() PURE;
};

/**
 * Encoder callback of request.
 */
class RequestEncoderCallback {
public:
  virtual ~RequestEncoderCallback() = default;

  /**
   * If request encoding success then this method will be called.
   * @param buffer encoding result buffer.
   * @param expect_response whether the current request requires an upstream response.
   */
  virtual void onEncodingSuccess(Buffer::Instance& buffer, bool expect_response) PURE;
};

/**
 * Encoder callback of Response.
 */
class ResponseEncoderCallback {
public:
  virtual ~ResponseEncoderCallback() = default;

  /**
   * If response encoding success then this method will be called.
   * @param buffer encoding result buffer.
   * @param close_connection whether the downstream connection should be closed.
   */
  virtual void onEncodingSuccess(Buffer::Instance& buffer, bool close_connection) PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
