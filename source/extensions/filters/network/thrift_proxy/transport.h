#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/fmt.h"
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

/*
 * TransportImplBase provides a base class for Transport implementations.
 */
class TransportImplBase : public virtual Transport {
public:
  TransportImplBase(TransportCallbacks& callbacks) : callbacks_(callbacks) {}

protected:
  void onFrameStart(absl::optional<uint32_t> size) const { callbacks_.transportFrameStart(size); }
  void onFrameComplete() const { callbacks_.transportFrameComplete(); }

  TransportCallbacks& callbacks_;
};

/**
 * FramedTransportImpl implements the Thrift Framed transport.
 * See https://github.com/apache/thrift/blob/master/doc/specs/thrift-rpc.md
 */
class FramedTransportImpl : public TransportImplBase {
public:
  FramedTransportImpl(TransportCallbacks& callbacks) : TransportImplBase(callbacks) {}

  // Transport
  const std::string& name() const override { return TransportNames::get().FRAMED; }
  bool decodeFrameStart(Buffer::Instance& buffer) override;
  bool decodeFrameEnd(Buffer::Instance& buffer) override;

  static const int32_t MaxFrameSize = 0xFA0000;
};

/**
 * UnframedTransportImpl implements the Thrift Unframed transport.
 * See https://github.com/apache/thrift/blob/master/doc/specs/thrift-rpc.md
 */
class UnframedTransportImpl : public TransportImplBase {
public:
  UnframedTransportImpl(TransportCallbacks& callbacks) : TransportImplBase(callbacks) {}

  // Transport
  const std::string& name() const override { return TransportNames::get().UNFRAMED; }
  bool decodeFrameStart(Buffer::Instance&) override {
    onFrameStart(absl::optional<uint32_t>());
    return true;
  }
  bool decodeFrameEnd(Buffer::Instance&) override {
    onFrameComplete();
    return true;
  }
};

/**
 * AutoTransportImpl implements Transport and attempts to distinguish between the Thrift framed and
 * unframed transports. Once the transport is detected, subsequent operations are delegated to the
 * appropriate implementation.
 */
class AutoTransportImpl : public TransportImplBase {
public:
  AutoTransportImpl(TransportCallbacks& callbacks)
      : TransportImplBase(callbacks), name_(TransportNames::get().AUTO){};

  // Transport
  const std::string& name() const override { return name_; }
  bool decodeFrameStart(Buffer::Instance& buffer) override;
  bool decodeFrameEnd(Buffer::Instance& buffer) override;

private:
  void setTransport(TransportPtr&& transport) {
    transport_ = std::move(transport);
    name_ = fmt::format("{}({})", transport_->name(), TransportNames::get().AUTO);
  }

  TransportPtr transport_{};
  std::string name_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
