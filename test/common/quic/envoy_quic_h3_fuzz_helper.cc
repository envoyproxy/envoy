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

static std::string encodeHeaders(const spdy::Http2HeaderBlock& headers) {
  static Delegate delegate;
  quic::QpackEncoder encoder(&delegate);
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

size_t H3Serializer::serialize(char* buffer, size_t buffer_len, bool unidirectional, uint32_t type,
                               uint32_t id, const H3Frame& h3frame) {
  quiche::QuicheDataWriter dw(buffer_len, buffer);
  if (unidirectional) {
    if (open_h3_streams_.find(id) == open_h3_streams_.end()) {
      dw.WriteVarInt62(static_cast<uint64_t>(type));
      open_h3_streams_.insert(id);
    }
  }
  switch (h3frame.frame_case()) {
  case H3Frame::kData: {
    const auto& f = h3frame.data();
    return buildRawFrame(dw, Type::Data, f.data());
  }
  case H3Frame::kHeaders: {
    const auto& f = h3frame.headers();
    spdy::Http2HeaderBlock headers;
    for (const auto& hdr : f.headers().headers()) {
      headers.AppendValueOrAddHeader(hdr.key(), hdr.value());
    }
    return buildRawFrame(dw, Type::Headers, encodeHeaders(headers));
  }
  case H3Frame::kCancelPush: {
    const auto& f = h3frame.cancel_push();
    return buildVarIntFrame(dw, Type::CancelPush, f.push_id());
  }
  case H3Frame::kSettings: {
    const auto& f = h3frame.settings();
    std::vector<std::pair<uint64_t, uint64_t>> values;
    for (auto& setting : f.settings()) {
      values.push_back(std::make_pair<uint64_t, uint64_t>(setting.identifier(), setting.value()));
    }
    return buildSettingsFrame(dw, values);
  }
  case H3Frame::kPushPromise: {
    const auto& f = h3frame.push_promise();
    uint64_t push_id = f.push_id();
    spdy::Http2HeaderBlock headers;
    for (auto& hdr : f.headers().headers()) {
      headers.AppendValueOrAddHeader(hdr.key(), hdr.value());
    }
    return buildPushPromiseFrame(dw, push_id, encodeHeaders(headers));
  }
  case H3Frame::kGoAway: {
    const auto& f = h3frame.go_away();
    return buildVarIntFrame(dw, Type::GoAway, f.push_id());
  }
  case H3Frame::kMaxPushId: {
    const auto& f = h3frame.max_push_id();
    return buildVarIntFrame(dw, Type::MaxPushId, f.push_id());
  }
  default:
    break;
  }
  return 0;
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
      h3serializer_(open_h3_streams_) {
  quic::Perspective p = quic::Perspective::IS_CLIENT;
  framer_.SetEncrypter(quic::ENCRYPTION_INITIAL, std::make_unique<FuzzEncrypter>(p));
  framer_.SetEncrypter(quic::ENCRYPTION_HANDSHAKE, std::make_unique<FuzzEncrypter>(p));
  framer_.SetEncrypter(quic::ENCRYPTION_ZERO_RTT, std::make_unique<FuzzEncrypter>(p));
  framer_.SetEncrypter(quic::ENCRYPTION_FORWARD_SECURE, std::make_unique<FuzzEncrypter>(p));
}

size_t QuicPacketizer::serializePackets(const QuicH3FuzzCase& input,
    QuicPacketizer::QuicPacket *packets, size_t max_packets) {
  size_t idx = 0;
  for (auto& quic_frame_or_junk : input.frames()) {
    if (idx >= max_packets) {
      return idx;
    }
    if (quic_frame_or_junk.has_qframe()) {
      if (serializePacket(quic_frame_or_junk.qframe(), &packets[idx])) {
        idx++;
      }
    } else if (quic_frame_or_junk.has_junk()) {
      const std::string& junk = quic_frame_or_junk.junk();
      if (serializeJunkPacket(junk, &packets[idx])) {
        idx++;
      }
    }
  }
  return idx;
}

void QuicPacketizer::reset() {
  packet_number_ = quic::QuicPacketNumber(0);
  open_h3_streams_.clear();
}

bool QuicPacketizer::serializePacket(const QuicFrame& frame, QuicPacketizer::QuicPacket *packet) {
  char buffer[1024];

  std::unique_ptr<quic::QuicCryptoFrame> crypto_frame = nullptr;
  std::unique_ptr<quic::QuicRstStreamFrame> reset_stream_frame = nullptr;
  std::unique_ptr<quic::QuicConnectionCloseFrame> connection_close_frame = nullptr;
  std::unique_ptr<quic::QuicGoAwayFrame> go_away_frame = nullptr;
  std::unique_ptr<quic::QuicRetireConnectionIdFrame> retire_connection_id_frame = nullptr;
  std::unique_ptr<quic::QuicMessageFrame> message_frame = nullptr;
  std::unique_ptr<quic::QuicNewTokenFrame> new_token = nullptr;
  std::unique_ptr<quic::QuicAckFrequencyFrame> ack_frequency = nullptr;
  quic::QuicAckFrame ack_frame;
  quic::QuicNewConnectionIdFrame new_connection_id_frame;

  quic::QuicPacketHeader header;
  header.packet_number = packet_number_;
  header.destination_connection_id = destination_connection_id_;
  header.source_connection_id = destination_connection_id_;
  packet_number_++;
  quic::QuicFrames frames;
  switch (frame.frame_case()) {
  case QuicFrame::kPadding: {
    int padding = frame.padding().num_padding_bytes() & 0xff;
    if (padding == 0) {
      padding++;
    }
    frames.push_back(quic::QuicFrame(quic::QuicPaddingFrame(padding)));
  } break;
  case QuicFrame::kStream: {
    const auto& stream = frame.stream();
    size_t len = 0;
    bool unidirectional = stream.unidirectional();
    uint32_t type = stream.type();
    uint32_t id = stream.id();
    bool fin = stream.fin();
    uint64_t offset = clampU64(stream.offset());
    if (stream.has_h3frame()) {
      const auto& f = stream.h3frame();
      len = h3serializer_.serialize(buffer, sizeof(buffer), unidirectional, type, id, f);
      if (len == 0) {
        return false;
      }
    } else if (stream.has_junk()) {
      auto junk = stream.junk();
      len = std::min(junk.size(), sizeof(buffer));
      memcpy(buffer, junk.data(), len);
    } else {
      return false;
    }
    frames.push_back(quic::QuicFrame(quic::QuicStreamFrame(id, fin, offset, buffer, len)));
  } break;
  case QuicFrame::kHandshakeDone: {
    const auto& f = frame.handshake_done();
    frames.push_back(quic::QuicFrame(quic::QuicHandshakeDoneFrame(f.control_frame_id())));
  } break;
  case QuicFrame::kCrypto: {
    const auto& f = frame.crypto();
    uint16_t len = std::min(f.data().size(), sizeof(buffer));
    memcpy(buffer, f.data().data(), len);
    crypto_frame = std::make_unique<quic::QuicCryptoFrame>(
        quic::EncryptionLevel::ENCRYPTION_INITIAL, clampU64(f.offset()), buffer, len);
    frames.push_back(quic::QuicFrame(crypto_frame.get()));
  } break;
  case QuicFrame::kAck: {
    const auto& f = frame.ack();
    auto largest_acked = quic::QuicPacketNumber(clampU64(f.largest_acked()));
    ack_frame.largest_acked = largest_acked;
    ack_frame.packets.Add(largest_acked);
    if (f.has_ecn_counters()) {
      const auto& c = f.ecn_counters();
      ack_frame.ecn_counters =
          quic::QuicEcnCounts(clampU64(c.ect0()), clampU64(c.ect1()), clampU64(c.ce()));
    }
    frames.push_back(quic::QuicFrame(&ack_frame));
  } break;
  case QuicFrame::kMtuDiscovery: {
    frames.push_back(quic::QuicFrame(quic::QuicMtuDiscoveryFrame()));
  } break;
  case QuicFrame::kStopWaiting: {
    // not possible in IETF mode
    return false;
  } break;
  case QuicFrame::kPing: {
    const auto& f = frame.ping();
    frames.push_back(quic::QuicFrame(quic::QuicPingFrame(f.control_frame_id())));
  } break;
  case QuicFrame::kRstStream: {
    const auto& f = frame.rst_stream();
    quic::QuicRstStreamErrorCode error_code =
        static_cast<quic::QuicRstStreamErrorCode>(f.error_code());
    reset_stream_frame = std::make_unique<quic::QuicRstStreamFrame>(
        f.control_frame_id(), f.stream_id(), error_code, clampU64(f.bytes_written()));
    frames.push_back(quic::QuicFrame(reset_stream_frame.get()));
  } break;
  case QuicFrame::kConnectionClose: {
    const auto& f = frame.connection_close();
    quic::QuicErrorCode error_code = toErrorCode(f.error_code());
    quic::QuicIetfTransportErrorCodes ietf_error =
        static_cast<quic::QuicIetfTransportErrorCodes>(clampU64(f.ietf_error()));
    connection_close_frame = std::make_unique<quic::QuicConnectionCloseFrame>(
        quic_version_.transport_version, error_code, ietf_error, f.error_phrase(),
        clampU64(f.transport_close_frame_type()));
    frames.push_back(quic::QuicFrame(connection_close_frame.get()));
  } break;
  case QuicFrame::kGoAway: {
    // not possible in IETF mode
    return false;
  } break;
  case QuicFrame::kWindowUpdate: {
    const auto& f = frame.window_update();
    frames.push_back(quic::QuicFrame(
        quic::QuicWindowUpdateFrame(f.control_frame_id(), f.stream_id(), clampU64(f.max_data()))));
  } break;
  case QuicFrame::kBlocked: {
    const auto& f = frame.blocked();
    frames.push_back(quic::QuicFrame(
        quic::QuicBlockedFrame(f.control_frame_id(), f.stream_id(), clampU64(f.offset()))));
  } break;
  case QuicFrame::kNewConnectionId: {
    const auto& f = frame.new_connection_id();
    new_connection_id_frame.control_frame_id = f.control_frame_id();
    new_connection_id_frame.connection_id = toConnectionId(f.connection_id());
    new_connection_id_frame.stateless_reset_token =
        toStatelessResetToken(f.stateless_reset_token());
    new_connection_id_frame.sequence_number = clampU64(f.sequence_number());
    frames.push_back(quic::QuicFrame(&new_connection_id_frame));
  } break;
  case QuicFrame::kRetireConnectionId: {
    // no retire frame in IETF mode
    const auto& f = frame.retire_connection_id();
    retire_connection_id_frame = std::make_unique<quic::QuicRetireConnectionIdFrame>(
        f.control_frame_id(), f.sequence_number());
    frames.push_back(quic::QuicFrame(retire_connection_id_frame.get()));

    return false;
  } break;
  case QuicFrame::kMaxStreams: {
    const auto& f = frame.max_streams();
    frames.push_back(quic::QuicFrame(
        quic::QuicMaxStreamsFrame(f.control_frame_id(), f.stream_count(), f.unidirectional())));
  } break;
  case QuicFrame::kStreamsBlocked: {
    const auto& f = frame.streams_blocked();
    frames.push_back(quic::QuicFrame(
        quic::QuicStreamsBlockedFrame(f.control_frame_id(), f.stream_count(), f.unidirectional())));
  } break;
  case QuicFrame::kPathResponse: {
    const auto& f = frame.path_response();
    frames.push_back(quic::QuicFrame(
        quic::QuicPathResponseFrame(f.control_frame_id(), toPathFrameBuffer(f.data()))));
  } break;
  case QuicFrame::kPathChallenge: {
    const auto& f = frame.path_challenge();
    frames.push_back(quic::QuicFrame(
        quic::QuicPathChallengeFrame(f.control_frame_id(), toPathFrameBuffer(f.data()))));
  } break;
  case QuicFrame::kStopSending: {
    const auto& f = frame.stop_sending();
    quic::QuicRstStreamErrorCode error_code =
        static_cast<quic::QuicRstStreamErrorCode>(f.error_code());
    frames.push_back(quic::QuicFrame(
        quic::QuicStopSendingFrame(f.control_frame_id(), f.stream_id(), error_code)));
  } break;
  case QuicFrame::kMessageFrame: {
    const auto& f = frame.message_frame();
    size_t len = std::min(f.data().size(), sizeof(buffer));
    memcpy(buffer, f.data().data(), len);
    message_frame = std::make_unique<quic::QuicMessageFrame>(buffer, len);
    frames.push_back(quic::QuicFrame(message_frame.get()));
  } break;
  case QuicFrame::kNewToken: {
    const auto& f = frame.new_token();
    size_t len = std::min(f.token().size(), sizeof(buffer));
    memcpy(buffer, f.token().data(), len);
    absl::string_view token(buffer, len);
    new_token = std::make_unique<quic::QuicNewTokenFrame>(f.control_frame_id(), token);
    frames.push_back(quic::QuicFrame(new_token.get()));
  } break;
  case QuicFrame::kAckFrequency: {
    const auto& f = frame.ack_frequency();
    auto delta = quic::QuicTime::Delta::FromMilliseconds(clampU64(f.milliseconds()));
    ack_frequency = std::make_unique<quic::QuicAckFrequencyFrame>(
        f.control_frame_id(), clampU64(f.sequence_number()), clampU64(f.packet_tolerance()), delta);
    frames.push_back(quic::QuicFrame(ack_frequency.get()));
  } break;
  default:
    return false;
  }
  quic::QuicFramer framer({quic_version_}, connection_helper_->GetClock()->Now(),
                          quic::Perspective::IS_CLIENT, quic::kQuicDefaultConnectionIdLength);

  auto encrypter = std::make_unique<FuzzEncrypter>(quic::Perspective::IS_CLIENT);
  framer.SetEncrypter(quic::ENCRYPTION_INITIAL, std::move(encrypter));
  packet->size =
      framer.BuildDataPacket(header, frames, packet->payload, sizeof(packet->payload),
                             quic::EncryptionLevel::ENCRYPTION_INITIAL);
  return packet->size > 0;
}

bool QuicPacketizer::serializeJunkPacket(const std::string& data, QuicPacketizer::QuicPacket *packet) {
  quic::QuicPacketHeader header;
  header.packet_number = packet_number_++;
  header.destination_connection_id = destination_connection_id_;
  header.source_connection_id = destination_connection_id_;
  quic::QuicDataWriter writer(sizeof(packet->payload), packet->payload);
  quic::QuicFramer framer({quic_version_}, connection_helper_->GetClock()->Now(),
                          quic::Perspective::IS_CLIENT, quic::kQuicDefaultConnectionIdLength);

  auto encrypter = std::make_unique<FuzzEncrypter>(quic::Perspective::IS_CLIENT);
  framer.SetEncrypter(quic::ENCRYPTION_INITIAL, std::move(encrypter));

  size_t length_field_offset = 0;
  if (!framer.AppendIetfPacketHeader(header, &writer, &length_field_offset)) {
    return false;
  }
  size_t max_data_len = std::min(data.size(), writer.remaining());
  writer.WriteBytes(data.data(), max_data_len);
  framer.WriteIetfLongHeaderLength(header, &writer, length_field_offset, quic::ENCRYPTION_INITIAL);
  packet->size = writer.length();
  return packet->size > 0;
}

} // namespace Quic
} // namespace Envoy
