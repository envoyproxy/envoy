#pragma once

#include "envoy/common/pure.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/dubbo_proxy/message.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * This class provides the pass-through capability of the original data of
 * the Dubbo protocol to improve the forwarding efficiency
 * when no modification of the original data is required.
 * Note: If the custom filter does not care about data transfer,
 *       then it does not need to care about this interface,
 *       which is currently used by router filter.
 */
class ProtocolDataPassthroughConverter {
public:
  ProtocolDataPassthroughConverter() = default;
  virtual ~ProtocolDataPassthroughConverter() = default;

  void initProtocolConverter(Buffer::Instance& buffer) { buffer_ = &buffer; }

  /**
   * Transfer the original header data of the Dubbo protocol,
   * it's called after the protocol's header data is parsed.
   * @param header_buf raw header data
   * @param size The size of the head.
   */
  virtual Network::FilterStatus transferHeaderTo(Buffer::Instance& header_buf, size_t size) {
    if (buffer_ != nullptr) {
      buffer_->move(header_buf, size);
    }
    return Network::FilterStatus::Continue;
  }

  /**
   * Transfer the original body data of the Dubbo protocol
   * it's called after the protocol's body data is parsed.
   * @param header_buf raw body data
   * @param size The size of the body.
   */
  virtual Network::FilterStatus transferBodyTo(Buffer::Instance& body_buf, size_t size) {
    if (buffer_ != nullptr) {
      buffer_->move(body_buf, size);
    }
    return Network::FilterStatus::Continue;
  }

protected:
  Buffer::Instance* buffer_{nullptr};
};

class DecoderEventHandler : public ProtocolDataPassthroughConverter {
public:
  ~DecoderEventHandler() override = default;

  /**
   * Indicates the start of a Dubbo transport data was detected. Unframed transports generate
   * simulated start messages.
   */
  virtual Network::FilterStatus transportBegin() PURE;

  /**
   * Indicates the end of a Dubbo transport data was detected. Unframed transport generate
   * simulated complete messages.
   */
  virtual Network::FilterStatus transportEnd() PURE;

  /**
   * Indicates that the start of a Dubbo protocol message was detected.
   * @param type the message type
   * @param message_id the message identifier
   * @param serialization_type the serialization type of the message
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual Network::FilterStatus messageBegin(MessageType type, int64_t message_id,
                                             SerializationType serialization_type) PURE;

  /**
   * Indicates that the end of a Dubbo protocol message was detected.
   * @param metadata MessageMetadataSharedPtr describing the message
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual Network::FilterStatus messageEnd(MessageMetadataSharedPtr metadata) PURE;
};

class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  /**
   * @return DecoderEventHandler& a new DecoderEventHandler for a message.
   */
  virtual DecoderEventHandler* newDecoderEventHandler() PURE;

  /**
   * Indicates that the message is a heartbeat.
   */
  virtual void onHeartbeat(MessageMetadataSharedPtr) {}
};

using DecoderEventHandlerSharedPtr = std::shared_ptr<DecoderEventHandler>;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
