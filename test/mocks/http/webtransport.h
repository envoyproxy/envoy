#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "envoy/http/codec.h"

#include "gmock/gmock.h"
#include "quiche/web_transport/web_transport.h"

namespace Envoy {
namespace Http {

// Reusable mock for the Http::WebTransportSession interface (envoy/http/codec.h). Lives in its own
// target rather than the broad http_mocks so consumers that do not need WebTransport (and the
// QUICHE dependency it pulls in) are unaffected.
class MockWebTransportSession : public WebTransportSession {
public:
  MockWebTransportSession();
  ~MockWebTransportSession() override;

  MOCK_METHOD(void, setWebTransportVisitor,
              (std::unique_ptr<webtransport::SessionVisitor> visitor));
  MOCK_METHOD(webtransport::Session*, rawWebTransportSession, ());
};

// Mock of QUICHE webtransport::Stream, for exercising the stream-level bridge.
class MockWebTransportStream : public webtransport::Stream {
public:
  MockWebTransportStream();
  ~MockWebTransportStream() override;

  MOCK_METHOD(webtransport::Stream::ReadResult, Read, (absl::Span<char> buffer), (override));
  MOCK_METHOD(webtransport::Stream::ReadResult, Read, (std::string * output), (override));
  MOCK_METHOD(size_t, ReadableBytes, (), (const, override));
  MOCK_METHOD(webtransport::Stream::PeekResult, PeekNextReadableRegion, (), (const, override));
  MOCK_METHOD(bool, SkipBytes, (size_t bytes), (override));
  MOCK_METHOD(absl::Status, Writev,
              (absl::Span<quiche::QuicheMemSlice> data,
               const webtransport::StreamWriteOptions& options),
              (override));
  MOCK_METHOD(bool, CanWrite, (), (const, override));
  MOCK_METHOD(void, SetPriority, (const webtransport::StreamPriority& priority), (override));
  MOCK_METHOD(void, ResetWithUserCode, (webtransport::StreamErrorCode error), (override));
  MOCK_METHOD(void, SendStopSending, (webtransport::StreamErrorCode error), (override));
  MOCK_METHOD(void, ResetDueToInternalError, (), (override));
  MOCK_METHOD(void, MaybeResetDueToStreamObjectGone, (), (override));
  MOCK_METHOD(void, SetVisitor, (std::unique_ptr<webtransport::StreamVisitor> visitor), (override));
  MOCK_METHOD(webtransport::StreamVisitor*, visitor, (), (override));
  MOCK_METHOD(webtransport::StreamId, GetStreamId, (), (const, override));
};

// Mock of QUICHE webtransport::Session, for exercising the session-level bridge's forwarding. The
// bridge only ever drives the webtransport::Session interface, so a mock of it is sufficient to
// exercise the forwarding logic without a live quic::WebTransportHttp3. Named with a "Raw" prefix
// to distinguish it from MockWebTransportSession, which mocks the Envoy-level
// Http::WebTransportSession interface used by installBridge().
class MockRawWebTransportSession : public webtransport::Session {
public:
  MockRawWebTransportSession();
  ~MockRawWebTransportSession() override;

  MOCK_METHOD(void, CloseSession,
              (webtransport::SessionErrorCode error_code, absl::string_view error_message),
              (override));
  MOCK_METHOD(webtransport::Stream*, AcceptIncomingBidirectionalStream, (), (override));
  MOCK_METHOD(webtransport::Stream*, AcceptIncomingUnidirectionalStream, (), (override));
  MOCK_METHOD(bool, CanOpenNextOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(bool, CanOpenNextOutgoingUnidirectionalStream, (), (override));
  MOCK_METHOD(webtransport::Stream*, OpenOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(webtransport::Stream*, OpenOutgoingUnidirectionalStream, (), (override));
  MOCK_METHOD(webtransport::Stream*, GetStreamById, (webtransport::StreamId id), (override));
  MOCK_METHOD(webtransport::DatagramStatus, SendOrQueueDatagram, (absl::string_view datagram),
              (override));
  MOCK_METHOD(uint64_t, GetMaxDatagramSize, (), (const, override));
  MOCK_METHOD(void, SetDatagramMaxTimeInQueue, (absl::Duration max_time_in_queue), (override));
  MOCK_METHOD(webtransport::DatagramStats, GetDatagramStats, (), (override));
  MOCK_METHOD(webtransport::SessionStats, GetSessionStats, (), (override));
  MOCK_METHOD(void, NotifySessionDraining, (), (override));
  MOCK_METHOD(void, SetOnDraining, (quiche::SingleUseCallback<void()> callback), (override));
  MOCK_METHOD(std::optional<std::string>, GetNegotiatedSubprotocol, (), // NOLINT(std::optional)
              (const, override));
  MOCK_METHOD(webtransport::Perspective, GetPerspective, (), (const, override));
  MOCK_METHOD(webtransport::UnderlyingProtocol, GetUnderlyingProtocol, (), (const, override));
};

} // namespace Http
} // namespace Envoy
