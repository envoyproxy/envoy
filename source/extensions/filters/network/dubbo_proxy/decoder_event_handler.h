#pragma once

#include "envoy/common/pure.h"
#include "envoy/network/filter.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/dubbo_proxy/message.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

enum class FilterStatus : uint8_t {
  // Continue filter chain iteration.
  Continue,
  // Do not iterate to any of the remaining filters in the chain. Returning
  // FilterDataStatus::Continue from decodeData()/encodeData() or calling
  // continueDecoding()/continueEncoding() MUST be called if continued filter iteration is desired.
  StopIteration,
  // Indicates that a retry is required for the reply message received.
  Retry,
};

class StreamDecoder {
public:
  virtual ~StreamDecoder() = default;

  /**
   * Indicates that the message had been decoded.
   * @param metadata MessageMetadataSharedPtr describing the message
   * @param ctx the message context information
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus onMessageDecoded(MessageMetadataSharedPtr metadata,
                                        ContextSharedPtr ctx) PURE;
};

using StreamDecoderSharedPtr = std::shared_ptr<StreamDecoder>;

class StreamEncoder {
public:
  virtual ~StreamEncoder() = default;

  /**
   * Indicates that the message had been encoded.
   * @param metadata MessageMetadataSharedPtr describing the message
   * @param ctx the message context information
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus onMessageEncoded(MessageMetadataSharedPtr metadata,
                                        ContextSharedPtr ctx) PURE;
};

using StreamEncoderSharedPtr = std::shared_ptr<StreamEncoder>;

class StreamHandler {
public:
  virtual ~StreamHandler() = default;

  /**
   * Indicates that the message had been decoded.
   * @param metadata MessageMetadataSharedPtr describing the message
   * @param ctx the message context information
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual void onStreamDecoded(MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) PURE;
};

using StreamDecoderSharedPtr = std::shared_ptr<StreamDecoder>;

class DecoderCallbacksBase {
public:
  virtual ~DecoderCallbacksBase() = default;

  /**
   * @return StreamDecoder* a new StreamDecoder for a message.
   */
  virtual StreamHandler& newStream() PURE;

  /**
   * Indicates that the message is a heartbeat.
   */
  virtual void onHeartbeat(MessageMetadataSharedPtr) PURE;
};

class RequestDecoderCallbacks : public DecoderCallbacksBase {};
class ResponseDecoderCallbacks : public DecoderCallbacksBase {};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
