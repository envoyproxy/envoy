#pragma once

#include "contrib/sip_proxy/filters/network/source/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

enum class QueryStatus {
  // Do grpc query
  Pending,

  // Existed in local cache
  Continue,

  // Not existed in local cache and not do grpc query
  Stop
};

enum class FilterStatus {
  // Continue filter chain iteration.
  Continue,

  // Stop iterating over filters in the filter chain.
  StopIteration
};

class DecoderEventHandler {
public:
  virtual ~DecoderEventHandler() = default;

  /**
   * Indicates the start of a Sip transport frame was detected. Unframed transports generate
   * simulated start messages.
   * @param metadata MessageMetadataSharedPtr describing as much as is currently known about the
   *                                          message
   */
  virtual FilterStatus transportBegin(MessageMetadataSharedPtr metadata) PURE;

  /**
   * Indicates the end of a Sip transport frame was detected. Unframed transport generate
   * simulated complete messages.
   */
  virtual FilterStatus transportEnd() PURE;

  /**
   * Indicates that the start of a Sip protocol message was detected.
   * @param metadata MessageMetadataSharedPtr describing the message
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus messageBegin(MessageMetadataSharedPtr metadata) PURE;

  /**
   * Indicates that the end of a Sip protocol message was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus messageEnd() PURE;
};

using DecoderEventHandlerSharedPtr = std::shared_ptr<DecoderEventHandler>;

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
