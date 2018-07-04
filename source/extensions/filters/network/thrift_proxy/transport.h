#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"

#include "common/singleton/const_singleton.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Names of available Transport implementations.
 */
class TransportNameValues {
public:
  // Framed transport
  const std::string FRAMED = "framed";

  // Unframed transport
  const std::string UNFRAMED = "unframed";

  // Auto-detection transport
  const std::string AUTO = "auto";
};

typedef ConstSingleton<TransportNameValues> TransportNames;

/**
 * TransportCallbacks are Thrift transport-level callbacks.
 */
class TransportCallbacks {
public:
  virtual ~TransportCallbacks() {}

  /**
   * Indicates the start of a Thrift transport frame was detected.
   * @param size the size of the message, if available to the transport
   */
  virtual void transportFrameStart(absl::optional<uint32_t> size) PURE;

  /**
   * Indicates the end of a Thrift transport frame was detected.
   */
  virtual void transportFrameComplete() PURE;
};

/**
 * Transport represents a Thrift transport. The Thrift transport is nominally a generic,
 * bi-directional byte stream. In Envoy we assume it always represents a network byte stream and
 * the Transport is just a mechanism for framing messages and passing message metadata.
 */
class Transport {
public:
  virtual ~Transport() {}

  /*
   * Returns this transport's name.
   *
   * @return std::string containing the transport name.
   */
  virtual const std::string& name() const PURE;

  /*
   * decodeFrameStart decodes the start of a transport message, potentially invoking callbacks.
   * If successful, the start of the frame is removed from the buffer.
   *
   * @param buffer the currently buffered thrift data.
   * @return bool true if a complete frame header was successfully consumed, false if more data
   *                 is required.
   * @throws EnvoyException if the data is not valid for this transport.
   */
  virtual bool decodeFrameStart(Buffer::Instance& buffer) PURE;

  /*
   * decodeFrameEnd decodes the end of a transport message, potentially invoking callbacks.
   * If successful, the end of the frame is removed from the buffer.
   *
   * @param buffer the currently buffered thrift data.
   * @return bool true if a complete frame trailer was successfully consumed, false if more data
   *                 is required.
   * @throws EnvoyException if the data is not valid for this transport.
   */
  virtual bool decodeFrameEnd(Buffer::Instance& buffer) PURE;
};

typedef std::unique_ptr<Transport> TransportPtr;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
