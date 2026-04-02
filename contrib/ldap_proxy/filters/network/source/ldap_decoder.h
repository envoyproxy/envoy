#pragma once

#include <cstdint>

#include "envoy/common/platform.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "contrib/ldap_proxy/filters/network/source/protocol_helpers.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LdapProxy {

enum class DecoderSignal {
  NeedMoreData,
  StartTlsDetected,
  PlaintextOperation,
  DecoderError,
  PipelineViolation,
};

// Callbacks from decoder to filter
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  virtual void onStartTlsRequest(uint32_t msg_id, size_t msg_length) PURE;
  virtual void onPlaintextOperation(uint32_t msg_id) PURE;
  virtual void onDecoderError() PURE;
  virtual void onPipelineViolation() PURE;
};

enum class DecoderState {
  Init,
  StartTlsRequestSeen,
  Encrypted,
  Closed,
};

class Decoder {
public:
  virtual ~Decoder() = default;

  virtual DecoderSignal inspect(const Buffer::Instance& data) PURE;
  virtual DecoderState state() const PURE;
  virtual void setEncrypted() PURE;
  virtual void setClosed() PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  explicit DecoderImpl(DecoderCallbacks* callbacks);
  ~DecoderImpl() override = default;

  DecoderSignal inspect(const Buffer::Instance& data) override;
  DecoderState state() const override { return state_; }
  void setEncrypted() override;
  void setClosed() override;

private:
  DecoderSignal inspectInit(const Buffer::Instance& data);
  bool isStartTlsRequest(const uint8_t* data, size_t len, size_t msg_len) const;

  DecoderCallbacks* callbacks_;
  DecoderState state_{DecoderState::Init};
};

} // namespace LdapProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
