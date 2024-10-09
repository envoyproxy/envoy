#include "test/common/quic/envoy_quic_h3_fuzz_helper.h"

#include "quiche/common/quiche_data_writer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace Quic {

static uint64_t clampU64(uint64_t in) { return in & ((1ULL << 62) - 1); }

using namespace test::common::quic;

enum class Type : uint8_t {
  Data = 0x00,
  Headers = 0x01,
  CancelPush = 0x03,
  Settings = 0x04,
  PushPromise = 0x05,
  GoAway = 0x07,
  MaxPushId = 0x0d,
};

class Delegate : public quic::QpackEncoder::DecoderStreamErrorDelegate {
public:
  void OnDecoderStreamError(quic::QuicErrorCode, absl::string_view) override{};
};

static std::string encodeHeaders(const quiche::HttpHeaderBlock& headers) {
  static Delegate delegate;
  quic::QpackEncoder encoder(&delegate, quic::HuffmanEncoding::kEnabled,
                             quic::CookieCrumbling::kEnabled);
  return encoder.EncodeHeaderList(0, headers, nullptr);
}

static size_t buildRawFrame(quiche::QuicheDataWriter& dw, Type type, const std::string& payload) {
  bool valid = true;
  valid &= dw.WriteVarInt62(static_cast<uint64_t>(type));
  valid &= dw.WriteStringPieceVarInt62(payload);
  return valid ? dw.length() : 0;
}

static size_t buildVarIntFrame(quiche::QuicheDataWriter& dw, Type type, uint64_t number) {
  bool valid = true;
  uint64_t s = quiche::QuicheDataWriter::GetVarInt62Len(clampU64(number));
  valid &= dw.WriteVarInt62(static_cast<uint64_t>(type));
  valid &= dw.WriteVarInt62(s);
  valid &= dw.WriteVarInt62(clampU64(number));
  return valid ? dw.length() : 0;
}

static size_t buildSettingsFrame(quiche::QuicheDataWriter& dw,
                                 std::vector<std::pair<uint64_t, uint64_t>>& settings) {
  bool valid = true;
  uint64_t slen = 0;
  for (auto pair : settings) {
    slen += quiche::QuicheDataWriter::GetVarInt62Len(clampU64(pair.first));
    slen += quiche::QuicheDataWriter::GetVarInt62Len(clampU64(pair.second));
  }
  valid &= dw.WriteVarInt62(static_cast<uint64_t>(Type::Settings));
  valid &= dw.WriteVarInt62(slen);
  for (auto pair : settings) {
    valid &= dw.WriteVarInt62(clampU64(pair.first));
    valid &= dw.WriteVarInt62(clampU64(pair.second));
  }
  return valid ? dw.length() : 0;
}

static size_t buildPushPromiseFrame(quiche::QuicheDataWriter& dw, uint64_t push_id,
                                    const std::string& headers) {
  bool valid = true;
  uint64_t s = quiche::QuicheDataWriter::GetVarInt62Len(clampU64(push_id));
  s += headers.size();

  valid &= dw.WriteVarInt62(static_cast<uint64_t>(Type::PushPromise));
  valid &= dw.WriteVarInt62(s);
  valid &= dw.WriteVarInt62(clampU64(push_id));
  valid &= dw.WriteBytes(headers.data(), headers.size());
  return valid ? dw.length() : 0;
}

std::string H3Serializer::serialize(bool unidirectional, uint32_t type, uint32_t id,
                                    const H3Frame& h3frame) {
  char buffer[kMaxPacketSize];
  quiche::QuicheDataWriter dw(kMaxPacketSize, buffer);
  if (unidirectional) {
    if (open_unidirectional_streams_.find(id) == open_unidirectional_streams_.end()) {
      dw.WriteVarInt62(static_cast<uint64_t>(type));
      open_unidirectional_streams_.insert(id);
    }
  }
  switch (h3frame.frame_case()) {
  case H3Frame::kData: {
    const auto& f = h3frame.data();
    size_t size = buildRawFrame(dw, Type::Data, f.data());
    return {buffer, size};
  }
  case H3Frame::kHeaders: {
    const auto& f = h3frame.headers();
    quiche::HttpHeaderBlock headers;
    for (const auto& hdr : f.headers().headers()) {
      headers.AppendValueOrAddHeader(hdr.key(), hdr.value());
    }
    size_t size = buildRawFrame(dw, Type::Headers, encodeHeaders(headers));
    return {buffer, size};
  }
  case H3Frame::kCancelPush: {
    const auto& f = h3frame.cancel_push();
    size_t size = buildVarIntFrame(dw, Type::CancelPush, f.push_id());
    return {buffer, size};
  }
  case H3Frame::kSettings: {
    const auto& f = h3frame.settings();
    std::vector<std::pair<uint64_t, uint64_t>> values;
    for (auto& setting : f.settings()) {
      values.push_back(std::make_pair<uint64_t, uint64_t>(setting.identifier(), setting.value()));
    }
    size_t size = buildSettingsFrame(dw, values);
    return {buffer, size};
  }
  case H3Frame::kPushPromise: {
    const auto& f = h3frame.push_promise();
    uint64_t push_id = f.push_id();
    quiche::HttpHeaderBlock headers;
    for (auto& hdr : f.headers().headers()) {
      headers.AppendValueOrAddHeader(hdr.key(), hdr.value());
    }
    size_t size = buildPushPromiseFrame(dw, push_id, encodeHeaders(headers));
    return {buffer, size};
  }
  case H3Frame::kGoAway: {
    const auto& f = h3frame.go_away();
    size_t size = buildVarIntFrame(dw, Type::GoAway, f.push_id());
    return {buffer, size};
  }
  case H3Frame::kMaxPushId: {
    const auto& f = h3frame.max_push_id();
    size_t size = buildVarIntFrame(dw, Type::MaxPushId, f.push_id());
    return {buffer, size};
  }
  default:
    break;
  }
  return "";
}

static quic::QuicConnectionId toConnectionId(const std::string& data) {
  uint8_t size = std::min(static_cast<uint8_t>(data.size()), quic::kQuicDefaultConnectionIdLength);
  return {data.data(), size};
}

static quic::StatelessResetToken toStatelessResetToken(const std::string& data) {
  quic::StatelessResetToken token = {0};
  size_t to_copy = std::min(data.size(), token.size());
  const char* start = data.data();
  const char* end = start + to_copy;
  std::copy(start, end, token.begin());
  return token;
}

static quic::QuicPathFrameBuffer toPathFrameBuffer(const std::string& data) {
  quic::QuicPathFrameBuffer buffer = {0};
  size_t to_copy = std::min(data.size(), buffer.size());
  const char* start = data.data();
  const char* end = start + to_copy;
  std::copy(start, end, buffer.begin());
  return buffer;
}

static quic::QuicErrorCode toErrorCode(uint32_t) { return quic::QuicErrorCode::QUIC_NO_ERROR; }

QuicPacketizer::QuicPacketizer(const quic::ParsedQuicVersion& quic_version,
                               quic::QuicConnectionHelperInterface* connection_helper)
    : quic_version_(quic_version), connection_helper_(connection_helper), packet_number_(0),
      destination_connection_id_(quic::test::TestConnectionId()),
      framer_({quic_version_}, connection_helper_->GetClock()->Now(), quic::Perspective::IS_CLIENT,
              quic::kQuicDefaultConnectionIdLength),
      h3serializer_(open_unidirectional_streams_) {
  quic::Perspective p = quic::Perspective::IS_CLIENT;
  framer_.SetEncrypter(quic::ENCRYPTION_INITIAL, std::make_unique<FuzzEncrypter>(p));
  framer_.SetEncrypter(quic::ENCRYPTION_HANDSHAKE, std::make_unique<FuzzEncrypter>(p));
  framer_.SetEncrypter(quic::ENCRYPTION_ZERO_RTT, std::make_unique<FuzzEncrypter>(p));
  framer_.SetEncrypter(quic::ENCRYPTION_FORWARD_SECURE, std::make_unique<FuzzEncrypter>(p));
}

std::vector<QuicPacketizer::QuicPacketPtr>
QuicPacketizer::serializePackets(const QuicH3FuzzCase& input) {
  std::vector<QuicPacketizer::QuicPacketPtr> packets;
  for (auto& quic_frame_or_junk : input.frames()) {
    if (quic_frame_or_junk.has_qframe()) {
      auto packet = serializePacket(quic_frame_or_junk.qframe());
      if (packet) {
        packets.push_back(std::move(packet));
      }
    } else if (quic_frame_or_junk.has_junk()) {
      const std::string& junk = quic_frame_or_junk.junk();
      auto packet = serializeJunkPacket(junk);
      if (packet) {
        packets.push_back(std::move(packet));
      }
    }
  }
  return packets;
}

void QuicPacketizer::reset() {
  packet_number_ = quic::QuicPacketNumber(0);
  open_unidirectional_streams_.clear();
}

QuicPacketizer::QuicPacketPtr QuicPacketizer::serializePacket(const QuicFrame& frame) {
  switch (frame.frame_case()) {
  case QuicFrame::kPadding: {
    int padding = frame.padding().num_padding_bytes() & 0xff;
    if (padding == 0) {
      padding++;
    }
    auto quic_padding = quic::QuicPaddingFrame(padding);
    return serialize(quic::QuicFrame(quic_padding));
  }
  case QuicFrame::kStream:
    return serializeStreamFrame(frame.stream());
  case QuicFrame::kHandshakeDone: {
    const auto& f = frame.handshake_done();
    auto handshake = quic::QuicHandshakeDoneFrame(f.control_frame_id());
    return serialize(quic::QuicFrame(handshake));
  }
  case QuicFrame::kCrypto:
    return serializeCryptoFrame(frame.crypto());
  case QuicFrame::kAck:
    return serializeAckFrame(frame.ack());
  case QuicFrame::kMtuDiscovery: {
    auto quic_mtu = quic::QuicMtuDiscoveryFrame();
    return serialize(quic::QuicFrame(quic_mtu));
  }
  case QuicFrame::kStopWaiting:
    // not possible in IETF mode
    break;
  case QuicFrame::kPing: {
    const auto& f = frame.ping();
    auto quic_ping = quic::QuicPingFrame(f.control_frame_id());
    return serialize(quic::QuicFrame(quic_ping));
  }
  case QuicFrame::kRstStream: {
    const auto& f = frame.rst_stream();
    quic::QuicRstStreamErrorCode error_code =
        static_cast<quic::QuicRstStreamErrorCode>(f.error_code());
    auto reset_stream_frame = quic::QuicRstStreamFrame(f.control_frame_id(), f.stream_id(),
                                                       error_code, clampU64(f.bytes_written()));
    return serialize(quic::QuicFrame(&reset_stream_frame));
  }
  case QuicFrame::kConnectionClose: {
    const auto& f = frame.connection_close();
    quic::QuicErrorCode error_code = toErrorCode(f.error_code());
    quic::QuicIetfTransportErrorCodes ietf_error =
        static_cast<quic::QuicIetfTransportErrorCodes>(clampU64(f.ietf_error()));
    auto connection_close_frame =
        quic::QuicConnectionCloseFrame(quic_version_.transport_version, error_code, ietf_error,
                                       f.error_phrase(), clampU64(f.transport_close_frame_type()));
    return serialize(quic::QuicFrame(&connection_close_frame));
  }
  case QuicFrame::kGoAway: {
    // not possible in IETF mode
    return nullptr;
  }
  case QuicFrame::kWindowUpdate: {
    const auto& f = frame.window_update();
    auto quic_window =
        quic::QuicWindowUpdateFrame(f.control_frame_id(), f.stream_id(), clampU64(f.max_data()));
    return serialize(quic::QuicFrame(quic_window));
  }
  case QuicFrame::kBlocked: {
    const auto& f = frame.blocked();
    auto quic_blocked =
        quic::QuicBlockedFrame(f.control_frame_id(), f.stream_id(), clampU64(f.offset()));
    return serialize(quic::QuicFrame(quic_blocked));
  }
  case QuicFrame::kNewConnectionId:
    return serializeNewConnectionIdFrame(frame.new_connection_id());
  case QuicFrame::kRetireConnectionId: {
    const auto& f = frame.retire_connection_id();
    auto retire_connection_id_frame =
        quic::QuicRetireConnectionIdFrame(f.control_frame_id(), clampU64(f.sequence_number()));
    return serialize(quic::QuicFrame(&retire_connection_id_frame));
  }
  case QuicFrame::kMaxStreams: {
    const auto& f = frame.max_streams();
    auto quic_max_streams =
        quic::QuicMaxStreamsFrame(f.control_frame_id(), f.stream_count(), f.unidirectional());
    return serialize(quic::QuicFrame(quic_max_streams));
  }
  case QuicFrame::kStreamsBlocked: {
    const auto& f = frame.streams_blocked();
    auto quic_streams =
        quic::QuicStreamsBlockedFrame(f.control_frame_id(), f.stream_count(), f.unidirectional());
    return serialize(quic::QuicFrame(quic_streams));
  }
  case QuicFrame::kPathResponse: {
    const auto& f = frame.path_response();
    auto quic_path = quic::QuicPathResponseFrame(f.control_frame_id(), toPathFrameBuffer(f.data()));
    return serialize(quic::QuicFrame(quic_path));
  }
  case QuicFrame::kPathChallenge: {
    const auto& f = frame.path_challenge();
    auto quic_path =
        quic::QuicPathChallengeFrame(f.control_frame_id(), toPathFrameBuffer(f.data()));
    return serialize(quic::QuicFrame(quic_path));
  }
  case QuicFrame::kStopSending: {
    const auto& f = frame.stop_sending();
    quic::QuicRstStreamErrorCode error_code =
        static_cast<quic::QuicRstStreamErrorCode>(f.error_code());
    auto quic_stop = quic::QuicStopSendingFrame(f.control_frame_id(), f.stream_id(), error_code);
    return serialize(quic::QuicFrame(quic_stop));
  }
  case QuicFrame::kMessageFrame:
    return serializeMessageFrame(frame.message_frame());
  case QuicFrame::kNewToken:
    return serializeNewTokenFrame(frame.new_token());
  case QuicFrame::kAckFrequency: {
    const auto& f = frame.ack_frequency();
    auto delta = quic::QuicTime::Delta::FromMilliseconds(clampU64(f.milliseconds()));
    auto ack_frequency = quic::QuicAckFrequencyFrame(
        f.control_frame_id(), clampU64(f.sequence_number()), clampU64(f.packet_tolerance()), delta);
    return serialize(quic::QuicFrame(&ack_frequency));
  }
  default:
    break;
  }
  return nullptr;
}

QuicPacketizer::QuicPacketPtr QuicPacketizer::serializeJunkPacket(const std::string& data) {
  char* buffer = new char[kMaxPacketSize];

  quic::QuicPacketHeader header;
  header.packet_number = packet_number_++;
  header.destination_connection_id = destination_connection_id_;
  header.source_connection_id = destination_connection_id_;

  quic::QuicDataWriter writer(kMaxPacketSize, buffer);
  quic::QuicFramer framer({quic_version_}, connection_helper_->GetClock()->Now(),
                          quic::Perspective::IS_CLIENT, quic::kQuicDefaultConnectionIdLength);

  auto encrypter = std::make_unique<FuzzEncrypter>(quic::Perspective::IS_CLIENT);
  framer.SetEncrypter(quic::ENCRYPTION_INITIAL, std::move(encrypter));

  size_t length_field_offset = 0;
  if (!framer.AppendIetfPacketHeader(header, &writer, &length_field_offset)) {
    return nullptr;
  }
  size_t max_data_len = std::min(data.size(), writer.remaining());
  writer.WriteBytes(data.data(), max_data_len);
  framer.WriteIetfLongHeaderLength(header, &writer, length_field_offset, quic::ENCRYPTION_INITIAL);
  return std::make_unique<quic::QuicEncryptedPacket>(buffer, writer.length(), true);
}

QuicPacketizer::QuicPacketPtr QuicPacketizer::serialize(quic::QuicFrame frame) {
  char* buffer = new char[kMaxPacketSize];
  quic::QuicFrames frames = {frame};
  quic::QuicFramer framer({quic_version_}, connection_helper_->GetClock()->Now(),
                          quic::Perspective::IS_CLIENT, quic::kQuicDefaultConnectionIdLength);

  quic::QuicPacketHeader header;
  header.packet_number = packet_number_++;
  header.destination_connection_id = destination_connection_id_;
  header.source_connection_id = destination_connection_id_;
  auto encrypter = std::make_unique<FuzzEncrypter>(quic::Perspective::IS_CLIENT);
  framer.SetEncrypter(quic::ENCRYPTION_INITIAL, std::move(encrypter));
  size_t size = framer.BuildDataPacket(header, frames, buffer, kMaxPacketSize,
                                       quic::EncryptionLevel::ENCRYPTION_INITIAL);
  return std::make_unique<quic::QuicEncryptedPacket>(buffer, size, true);
}

QuicPacketizer::QuicPacketPtr
QuicPacketizer::serializeStreamFrame(const test::common::quic::QuicStreamFrame& frame) {
  bool unidirectional = frame.unidirectional();
  uint32_t type = frame.type();
  uint32_t id = frame.id();
  bool fin = frame.fin();
  uint64_t offset = clampU64(frame.offset());
  if (frame.has_h3frame()) {
    const auto& f = frame.h3frame();
    auto h3packet = h3serializer_.serialize(unidirectional, type, id, f);
    if (!h3packet.empty()) {
      return serialize(quic::QuicFrame(
          quic::QuicStreamFrame(id, fin, offset, h3packet.data(), h3packet.size())));
    }
  } else if (frame.has_junk()) {
    auto junk = frame.junk();
    size_t len = std::min(junk.size(), 1024UL);
    return serialize(quic::QuicFrame(quic::QuicStreamFrame(id, fin, offset, junk.data(), len)));
  }
  return nullptr;
}

QuicPacketizer::QuicPacketPtr
QuicPacketizer::serializeNewTokenFrame(const test::common::quic::QuicNewTokenFrame& frame) {
  char buffer[1024];
  size_t len = std::min(frame.token().size(), sizeof(buffer));
  memcpy(buffer, frame.token().data(), len);
  absl::string_view token(buffer, len);
  auto new_token = quic::QuicNewTokenFrame(frame.control_frame_id(), token);
  return serialize(quic::QuicFrame(&new_token));
}

QuicPacketizer::QuicPacketPtr
QuicPacketizer::serializeMessageFrame(const test::common::quic::QuicMessageFrame& frame) {
  char buffer[1024];
  auto message = frame.data();
  size_t len = std::min(message.size(), sizeof(buffer));
  memcpy(buffer, message.data(), len);
  auto message_frame = quic::QuicMessageFrame(buffer, len);
  return serialize(quic::QuicFrame(&message_frame));
}

QuicPacketizer::QuicPacketPtr
QuicPacketizer::serializeCryptoFrame(const test::common::quic::QuicCryptoFrame& frame) {
  char buffer[1024];
  auto data = frame.data();
  uint16_t len = std::min(data.size(), sizeof(buffer));
  memcpy(buffer, data.data(), len);
  auto crypto_frame = quic::QuicCryptoFrame(quic::EncryptionLevel::ENCRYPTION_INITIAL,
                                            clampU64(frame.offset()), buffer, len);
  return serialize(quic::QuicFrame(&crypto_frame));
}

QuicPacketizer::QuicPacketPtr
QuicPacketizer::serializeAckFrame(const test::common::quic::QuicAckFrame& frame) {
  auto largest_acked = quic::QuicPacketNumber(clampU64(frame.largest_acked()));
  quic::QuicAckFrame ack_frame;
  ack_frame.largest_acked = largest_acked;
  ack_frame.packets.Add(largest_acked);
  if (frame.has_ecn_counters()) {
    const auto& c = frame.ecn_counters();
    ack_frame.ecn_counters =
        quic::QuicEcnCounts(clampU64(c.ect0()), clampU64(c.ect1()), clampU64(c.ce()));
  }
  return serialize(quic::QuicFrame(&ack_frame));
}

QuicPacketizer::QuicPacketPtr QuicPacketizer::serializeNewConnectionIdFrame(
    const test::common::quic::QuicNewConnectionIdFrame& frame) {
  quic::QuicNewConnectionIdFrame new_connection_id_frame;
  new_connection_id_frame.control_frame_id = frame.control_frame_id();
  new_connection_id_frame.connection_id = toConnectionId(frame.connection_id());
  new_connection_id_frame.stateless_reset_token =
      toStatelessResetToken(frame.stateless_reset_token());
  new_connection_id_frame.sequence_number = clampU64(frame.sequence_number());
  return serialize(quic::QuicFrame(&new_connection_id_frame));
}

} // namespace Quic
} // namespace Envoy
