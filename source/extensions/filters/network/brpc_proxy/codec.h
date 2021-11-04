#pragma once

#include <string>

#include "envoy/common/exception.h"
#include "source/extensions/filters/network/brpc_proxy/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

/**
 * Callbacks that the decoder fires.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  /**
   * Called when a new top level brpc Message has been decoded.
   * @param value supplies the decoded value that is now owned by the callee.
   */
  virtual void onMessage(BrpcMessagePtr&& value) PURE;
};

/**
 * A  byte decoder for brpc protocol
 */
class Decoder {
public:
  virtual ~Decoder() = default;

  /**
   * Decode brpc protocol bytes.
   * @param data supplies the data to decode. All bytes will be consumed by the decoder or a
   *        ProtocolError will be thrown.
   */
  virtual void decode(Buffer::Instance& data) PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

/**
 * A factory for a brpc decoder.
 */
class DecoderFactory {
public:
  virtual ~DecoderFactory() = default;

  /**
   * Create a decoder given a set of decoder callbacks.
   */
  virtual DecoderPtr create(DecoderCallbacks& callbacks) PURE;
};

/**
 * A byte encoder for brpc protocol
 */
class Encoder {
public:
  virtual ~Encoder() = default;

  /**
   * Encode a Brpc Message to a buffer.
   * @param value supplies the value to encode.
   * @param out supplies the buffer to encode to.
   */
  virtual void encode(BrpcMessage& value, Buffer::Instance& out) PURE;
};

using EncoderPtr = std::unique_ptr<Encoder>;
} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

