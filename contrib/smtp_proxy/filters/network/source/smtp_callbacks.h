#pragma once
#include <cstdint>

#include "envoy/common/platform.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_stats.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// General callbacks for dispatching decoded SMTP messages to a sink.
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  virtual void incSmtpTransactionRequests() PURE;
  virtual void incSmtpTransactionsCompleted() PURE;
  virtual void incSmtpTransactionsAborted() PURE;
  virtual void incSmtpTrxnFailed() PURE;
  virtual void incSmtpSessionRequests() PURE;
  virtual void incSmtpConnectionEstablishmentErrors() PURE;
  virtual void incSmtpSessionsCompleted() PURE;
  virtual void incSmtpSessionsTerminated() PURE;
  virtual void incTlsTerminatedSessions() PURE;
  virtual void incTlsTerminationErrors() PURE;
  virtual void incUpstreamTlsSuccess() PURE;
  virtual void incUpstreamTlsFailed() PURE;
  virtual void incActiveTransaction() PURE;
  virtual void decActiveTransaction() PURE;
  virtual void incActiveSession() PURE;
  virtual void decActiveSession() PURE;
  virtual SmtpProxyStats& getStats() PURE;

  virtual void incSmtpAuthErrors() PURE;
  virtual void incMailDataTransferErrors() PURE;
  virtual void incMailRcptErrors() PURE;
  virtual void inc4xxErrors() PURE;
  virtual void inc5xxErrors() PURE;

  virtual bool downstreamStartTls(absl::string_view) PURE;
  virtual bool sendReplyDownstream(absl::string_view) PURE;
  virtual bool upstreamTlsEnabled() const PURE;
  virtual bool downstreamTlsEnabled() const PURE;
  virtual bool downstreamTlsRequired() const PURE;
  virtual bool upstreamTlsRequired() const PURE;
  virtual bool protocolInspectionEnabled() const PURE;
  virtual bool tracingEnabled() PURE;
  virtual bool upstreamStartTls() PURE;
  virtual bool sendUpstream(Buffer::Instance&) PURE;
  virtual Buffer::OwnedImpl& getReadBuffer() PURE;
  virtual Buffer::OwnedImpl& getWriteBuffer() PURE;
  virtual void closeDownstreamConnection() PURE;
  virtual Network::Connection& connection() const PURE;
  virtual StreamInfo::StreamInfo& getStreamInfo() PURE;
  virtual void emitLogEntry(StreamInfo::StreamInfo&) PURE;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
