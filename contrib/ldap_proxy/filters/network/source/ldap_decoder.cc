#include "contrib/ldap_proxy/filters/network/source/ldap_decoder.h"

#include <cstring>
#include <vector>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LdapProxy {

constexpr char kStartTlsOidStr[] = "1.3.6.1.4.1.1466.20037";
constexpr size_t kStartTlsOidStrLen = sizeof(kStartTlsOidStr) - 1;
constexpr uint8_t kBerContextTag0 = 0x80;

DecoderImpl::DecoderImpl(DecoderCallbacks* callbacks) : callbacks_(callbacks) {}

void DecoderImpl::setEncrypted() {
  if (state_ != DecoderState::Closed) {
    state_ = DecoderState::Encrypted;
  }
}

void DecoderImpl::setClosed() { state_ = DecoderState::Closed; }

DecoderSignal DecoderImpl::inspect(const Buffer::Instance& data) {
  switch (state_) {
  case DecoderState::Init:
    return inspectInit(data);

  case DecoderState::StartTlsRequestSeen:
    callbacks_->onPipelineViolation();
    return DecoderSignal::PipelineViolation;

  case DecoderState::Encrypted:
    return DecoderSignal::PlaintextOperation;

  case DecoderState::Closed:
    return DecoderSignal::DecoderError;
  }

  return DecoderSignal::DecoderError;
}

DecoderSignal DecoderImpl::inspectInit(const Buffer::Instance& data) {
  const size_t data_len = data.length();

  if (data_len > kMaxInspectBufferSize) {
    ENVOY_LOG(debug, "ldap_decoder: buffer exceeds max inspect size ({} > {})", data_len,
              kMaxInspectBufferSize);
    callbacks_->onDecoderError();
    return DecoderSignal::DecoderError;
  }

  if (data_len < 2) {
    return DecoderSignal::NeedMoreData;
  }

  std::vector<uint8_t> buf_storage(data_len);
  data.copyOut(0, data_len, buf_storage.data());
  const uint8_t* buf = buf_storage.data();

  auto [complete, msg_len] = isCompleteBerMessage(buf, data_len);
  if (!complete) {
    if (msg_len == 0 && data_len >= 2) {
      ENVOY_LOG(debug, "ldap_decoder: invalid BER structure");
      callbacks_->onDecoderError();
      return DecoderSignal::DecoderError;
    }
    return DecoderSignal::NeedMoreData;
  }

  auto msg_id_result = extractMessageId(buf, data_len);
  if (!msg_id_result.valid) {
    ENVOY_LOG(debug, "ldap_decoder: failed to extract message ID");
    callbacks_->onDecoderError();
    return DecoderSignal::DecoderError;
  }

  if (isStartTlsRequest(buf, data_len, msg_len)) {
    if (data_len > msg_len) {
      ENVOY_LOG(debug, "ldap_decoder: pipeline attack detected - extra bytes after StartTLS");
      callbacks_->onPipelineViolation();
      return DecoderSignal::PipelineViolation;
    }

    ENVOY_LOG(debug, "ldap_decoder: StartTLS request detected, msgID={}", msg_id_result.message_id);
    state_ = DecoderState::StartTlsRequestSeen;
    callbacks_->onStartTlsRequest(msg_id_result.message_id, msg_len);
    return DecoderSignal::StartTlsDetected;
  }

  ENVOY_LOG(debug, "ldap_decoder: plaintext operation detected, msgID={}", msg_id_result.message_id);
  callbacks_->onPlaintextOperation(msg_id_result.message_id);
  return DecoderSignal::PlaintextOperation;
}

bool DecoderImpl::isStartTlsRequest(const uint8_t* data, size_t len, size_t /*msg_len*/) const {
  if (len < 10) {
    return false;
  }

  size_t offset = 0;

  if (data[offset] != kBerTagSequence) {
    return false;
  }
  offset++;

  auto seq_len = extractBerLength(data, len, offset);
  if (!seq_len.valid) {
    return false;
  }
  offset += seq_len.header_bytes;

  if (offset >= len || data[offset] != kBerTagInteger) {
    return false;
  }
  offset++;

  auto msg_id_len = extractBerLength(data, len, offset);
  if (!msg_id_len.valid) {
    return false;
  }
  offset += msg_id_len.header_bytes + msg_id_len.length;

  if (offset >= len || data[offset] != kLdapExtendedRequestTag) {
    return false;
  }
  offset++;

  auto ext_len = extractBerLength(data, len, offset);
  if (!ext_len.valid) {
    return false;
  }
  offset += ext_len.header_bytes;

  if (offset >= len || data[offset] != kBerContextTag0) {
    return false;
  }
  offset++;

  auto oid_len = extractBerLength(data, len, offset);
  if (!oid_len.valid) {
    return false;
  }
  offset += oid_len.header_bytes;

  if (offset + oid_len.length > len || oid_len.length != kStartTlsOidStrLen) {
    return false;
  }

  return std::memcmp(data + offset, kStartTlsOidStr, kStartTlsOidStrLen) == 0;
}

} // namespace LdapProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
