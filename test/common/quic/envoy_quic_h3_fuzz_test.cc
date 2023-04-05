#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_dispatcher.h"
#include "source/common/quic/envoy_quic_server_connection.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/platform/quiche_logging_impl.h"
#include "source/common/quic/server_codec_impl.h"

#include "test/common/quic/envoy_quic_h3_fuzz.pb.h"
#include "test/common/quic/test_proof_source.h"
#include "test/common/quic/test_utils.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"

#include "quiche/common/quiche_data_writer.h"
#include "quiche/quic/core/crypto/null_decrypter.h"
#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "quiche/quic/core/quic_crypto_server_stream.h"
#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/tls_server_handshaker.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace Quic {

const size_t kMaxNumPackets = 10;

const size_t kMaxQuicPacketSize = 1460;
static char quic_packets[kMaxNumPackets][kMaxQuicPacketSize];
static size_t quic_packet_sizes[kMaxNumPackets];

const size_t kMaxH3PacketSize = 1024;
static char h3_packet[kMaxH3PacketSize];

static uint64_t clampU64(uint64_t in) { return in & ((1ULL << 62) - 1); }

std::set<uint32_t> open_h3_streams;

using namespace test::common::quic;
class H3Packetizer {
private:
  enum class Type : uint8_t {
    Data = 0x00,
    Headers = 0x01,
    CancelPush = 0x03,
    Settings = 0x04,
    PushPromise = 0x05,
    GoAway = 0x07,
    MaxPushId = 0x0d,
  };

public:
  size_t serialize(bool unidirectional, uint32_t type, uint32_t id, const H3Frame& h3frame) {
    quiche::QuicheDataWriter dw(kMaxH3PacketSize, h3_packet);
    if (unidirectional) {
      if (open_h3_streams.find(id) == open_h3_streams.end()) {
        dw.WriteVarInt62(static_cast<uint64_t>(type));
        open_h3_streams.insert(id);
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
      for (const auto &hdr : f.headers().headers()) {
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

private:
  class Delegate : public quic::QpackEncoder::DecoderStreamErrorDelegate {
  public:
    void OnDecoderStreamError(quic::QuicErrorCode, absl::string_view) override{};
  };

  static std::string encodeHeaders(const spdy::Http2HeaderBlock& headers) {
    static Delegate delegate;
    quic::QpackEncoder encoder(&delegate);
    return encoder.EncodeHeaderList(0, headers, nullptr);
  }

  size_t buildRawFrame(quiche::QuicheDataWriter& dw, Type type, const std::string& payload) {
    bool valid = true;
    valid &= dw.WriteVarInt62(static_cast<uint64_t>(type));
    valid &= dw.WriteStringPieceVarInt62(payload);
    return valid ? dw.length() : 0;
  }

  size_t buildVarIntFrame(quiche::QuicheDataWriter& dw, Type type, uint64_t number) {
    bool valid = true;
    uint64_t s = quiche::QuicheDataWriter::GetVarInt62Len(clampU64(number));
    valid &= dw.WriteVarInt62(static_cast<uint64_t>(type));
    valid &= dw.WriteVarInt62(s);
    valid &= dw.WriteVarInt62(clampU64(number));
    return valid ? dw.length() : 0;
  }

  size_t buildSettingsFrame(quiche::QuicheDataWriter& dw,
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

  size_t buildPushPromiseFrame(quiche::QuicheDataWriter& dw, uint64_t push_id,
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
};

class FuzzEncrypter : public quic::QuicEncrypter {
public:
  bool SetKey(absl::string_view key) override { return key.empty(); };
  bool SetNoncePrefix(absl::string_view nonce_prefix) override { return nonce_prefix.empty(); };
  bool SetIV(absl::string_view iv) override { return iv.empty(); };
  bool SetHeaderProtectionKey(absl::string_view key) override { return key.empty(); };
  size_t GetKeySize() const override { return 0; };
  size_t GetNoncePrefixSize() const override { return 0; };
  size_t GetIVSize() const override { return 0; };
  bool EncryptPacket(uint64_t, absl::string_view, absl::string_view plaintext, char* output,
                     size_t* output_length, size_t max_output_length) override {
    ASSERT(plaintext.length() <= max_output_length);
    memcpy(output, plaintext.data(), plaintext.length());
    *output_length = plaintext.length();
    return true;
  };
  std::string GenerateHeaderProtectionMask(absl::string_view) override {
    return std::string(5, 0);
  };
  size_t GetMaxPlaintextSize(size_t ciphertext_size) const override { return ciphertext_size; }
  size_t GetCiphertextSize(size_t plaintext_size) const override { return plaintext_size; }
  absl::string_view GetKey() const override { return {}; };
  absl::string_view GetNoncePrefix() const override { return {}; };
  quic::QuicPacketCount GetConfidentialityLimit() const override {
    return std::numeric_limits<quic::QuicPacketCount>::max();
  }
};

class QuicPacketizer {
public:
  QuicPacketizer(const quic::ParsedQuicVersion& quic_version,
                 quic::QuicConnectionHelperInterface* connection_helper)
      : quic_version_(quic_version), connection_helper_(connection_helper), packet_number_(0),
        destination_connection_id_(quic::test::TestConnectionId()),
        framer_({quic_version_}, connection_helper_->GetClock()->Now(),
                quic::Perspective::IS_CLIENT, quic::kQuicDefaultConnectionIdLength) {
    framer_.SetEncrypter(quic::ENCRYPTION_INITIAL,
                         std::unique_ptr<quic::QuicEncrypter>(new FuzzEncrypter()));
    framer_.SetEncrypter(quic::ENCRYPTION_HANDSHAKE,
                         std::unique_ptr<quic::QuicEncrypter>(new FuzzEncrypter()));
    framer_.SetEncrypter(quic::ENCRYPTION_ZERO_RTT,
                         std::unique_ptr<quic::QuicEncrypter>(new FuzzEncrypter()));
    framer_.SetEncrypter(quic::ENCRYPTION_FORWARD_SECURE,
                         std::unique_ptr<quic::QuicEncrypter>(new FuzzEncrypter()));
  }

  void serializePackets(const QuicH3FuzzCase& input) {
    for (auto& quic_frame_or_junk : input.frames()) {
      if (idx_ >= kMaxNumPackets) {
        return;
      }
      if (quic_frame_or_junk.has_qframe()) {
        serializePacket(quic_frame_or_junk.qframe());
      } else if (quic_frame_or_junk.has_junk()) {
        const std::string& junk = quic_frame_or_junk.junk();
        size_t len = std::min(junk.size(), kMaxQuicPacketSize);
        std::memcpy(quic_packets[idx_], junk.data(), len);
        quic_packet_sizes[idx_] = len;
        idx_++;
      }
    }
  }

  void reset() {
    idx_ = 0;
    packet_number_ = quic::QuicPacketNumber(0);
    for (unsigned long& quic_packet_size : quic_packet_sizes) {
      quic_packet_size = 0;
    }
  }

#define PUSH(frame) frames.push_back(quic::QuicFrame(frame))
private:
  void serializePacket(const QuicFrame& frame) {
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
      PUSH(quic::QuicPaddingFrame(padding));
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
        H3Packetizer h3packetizer;
        len = h3packetizer.serialize(unidirectional, type, id, f);
        if (len == 0) {
          return;
        }
      } else if (stream.has_junk()) {
        auto junk = stream.junk();
        len = std::min(junk.size(), sizeof(h3_packet));
        memcpy(h3_packet, junk.data(), len);
      } else {
        return;
      }
      PUSH(quic::QuicStreamFrame(id, fin, offset, h3_packet, len));
    } break;
    case QuicFrame::kHandshakeDone: {
      const auto& f = frame.handshake_done();
      PUSH(quic::QuicHandshakeDoneFrame(f.control_frame_id()));
    } break;
    case QuicFrame::kCrypto: {
      const auto& f = frame.crypto();
      uint16_t len = std::min(f.data().size(), sizeof(h3_packet));
      memcpy(h3_packet, f.data().data(), len);
      crypto_frame = std::make_unique<quic::QuicCryptoFrame>(
          quic::EncryptionLevel::ENCRYPTION_INITIAL, clampU64(f.offset()), h3_packet, len);
      PUSH(crypto_frame.get());
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
      PUSH(&ack_frame);
    } break;
    case QuicFrame::kMtuDiscovery: {
      PUSH(quic::QuicMtuDiscoveryFrame());
    } break;
    case QuicFrame::kStopWaiting: {
      // not possible in IETF mode
      return;
    } break;
    case QuicFrame::kPing: {
      const auto& f = frame.ping();
      PUSH(quic::QuicPingFrame(f.control_frame_id()));
    } break;
    case QuicFrame::kRstStream: {
      const auto& f = frame.rst_stream();
      quic::QuicRstStreamErrorCode error_code =
          static_cast<quic::QuicRstStreamErrorCode>(f.error_code());
      reset_stream_frame = std::make_unique<quic::QuicRstStreamFrame>(
          f.control_frame_id(), f.stream_id(), error_code, clampU64(f.bytes_written()));
      PUSH(reset_stream_frame.get());
    } break;
    case QuicFrame::kConnectionClose: {
      const auto& f = frame.connection_close();
      quic::QuicErrorCode error_code = toErrorCode(f.error_code());
      quic::QuicIetfTransportErrorCodes ietf_error =
          static_cast<quic::QuicIetfTransportErrorCodes>(clampU64(f.ietf_error()));
      connection_close_frame = std::make_unique<quic::QuicConnectionCloseFrame>(
          quic_version_.transport_version, error_code, ietf_error, f.error_phrase(),
          clampU64(f.transport_close_frame_type()));
      PUSH(connection_close_frame.get());
    } break;
    case QuicFrame::kGoAway: {
      // not possible in IETF mode
      return;
    } break;
    case QuicFrame::kWindowUpdate: {
      const auto& f = frame.window_update();
      PUSH(
          quic::QuicWindowUpdateFrame(f.control_frame_id(), f.stream_id(), clampU64(f.max_data())));
    } break;
    case QuicFrame::kBlocked: {
      const auto& f = frame.blocked();
      PUSH(quic::QuicBlockedFrame(f.control_frame_id(), f.stream_id(), clampU64(f.offset())));
    } break;
    case QuicFrame::kNewConnectionId: {
      const auto& f = frame.new_connection_id();
      new_connection_id_frame.control_frame_id = f.control_frame_id();
      new_connection_id_frame.connection_id = toConnectionId(f.connection_id());
      new_connection_id_frame.stateless_reset_token =
          toStatelessResetToken(f.stateless_reset_token());
      new_connection_id_frame.sequence_number = clampU64(f.sequence_number());
      PUSH(&new_connection_id_frame);
    } break;
    case QuicFrame::kRetireConnectionId: {
      // no retire frame in IETF mode
      return;
    } break;
    case QuicFrame::kMaxStreams: {
      const auto& f = frame.max_streams();
      PUSH(quic::QuicMaxStreamsFrame(f.control_frame_id(), f.stream_count(), f.unidirectional()));
    } break;
    case QuicFrame::kStreamsBlocked: {
      const auto& f = frame.streams_blocked();
      PUSH(quic::QuicStreamsBlockedFrame(f.control_frame_id(), f.stream_count(),
                                         f.unidirectional()));
    } break;
    case QuicFrame::kPathResponse: {
      const auto& f = frame.path_response();
      PUSH(quic::QuicPathResponseFrame(f.control_frame_id(), toPathFrameBuffer(f.data())));
    } break;
    case QuicFrame::kPathChallenge: {
      const auto& f = frame.path_challenge();
      PUSH(quic::QuicPathChallengeFrame(f.control_frame_id(), toPathFrameBuffer(f.data())));
    } break;
    case QuicFrame::kStopSending: {
      const auto& f = frame.stop_sending();
      quic::QuicRstStreamErrorCode error_code =
          static_cast<quic::QuicRstStreamErrorCode>(f.error_code());
      PUSH(quic::QuicStopSendingFrame(f.control_frame_id(), f.stream_id(), error_code));
    } break;
    case QuicFrame::kMessageFrame: {
      const auto& f = frame.message_frame();
      size_t len = std::min(f.data().size(), sizeof(h3_packet));
      memcpy(h3_packet, f.data().data(), len);
      message_frame = std::make_unique<quic::QuicMessageFrame>(h3_packet, len);
      PUSH(message_frame.get());
    } break;
    case QuicFrame::kNewToken: {
      const auto& f = frame.new_token();
      size_t len = std::min(f.token().size(), sizeof(h3_packet));
      memcpy(h3_packet, f.token().data(), len);
      absl::string_view token(h3_packet, len);
      new_token = std::make_unique<quic::QuicNewTokenFrame>(f.control_frame_id(), token);
      PUSH(new_token.get());
    } break;
    case QuicFrame::kAckFrequency: {
      const auto& f = frame.ack_frequency();
      auto delta = quic::QuicTime::Delta::FromMilliseconds(clampU64(f.milliseconds()));
      ack_frequency = std::make_unique<quic::QuicAckFrequencyFrame>(
          f.control_frame_id(), clampU64(f.sequence_number()), clampU64(f.packet_tolerance()),
          delta);
      PUSH(ack_frequency.get());
    } break;
    default:
      return;
    }
    quic::QuicFramer framer({quic_version_}, connection_helper_->GetClock()->Now(),
                            quic::Perspective::IS_CLIENT, quic::kQuicDefaultConnectionIdLength);

    framer.SetEncrypter(quic::ENCRYPTION_INITIAL,
                        std::unique_ptr<quic::QuicEncrypter>(new FuzzEncrypter()));
    quic_packet_sizes[idx_] =
        framer.BuildDataPacket(header, frames, quic_packets[idx_], kMaxQuicPacketSize,
                               quic::EncryptionLevel::ENCRYPTION_INITIAL);
    idx_++;
  }
#undef PUSH

  static quic::QuicConnectionId toConnectionId(const std::string& data) {
    uint8_t size =
        std::min(static_cast<uint8_t>(data.size()), quic::kQuicDefaultConnectionIdLength);
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

  quic::ParsedQuicVersion quic_version_;
  quic::QuicConnectionHelperInterface* connection_helper_;
  quic::QuicPacketNumber packet_number_;
  size_t idx_{0};

  // For fuzzing
  quic::QuicConnectionId destination_connection_id_;
  quic::QuicFramer framer_;
};

class ProofSourceDetailsSetter {
public:
  virtual ~ProofSourceDetailsSetter() = default;

  virtual void setProofSourceDetails(std::unique_ptr<EnvoyQuicProofSourceDetails> details) = 0;
};

class TestQuicCryptoServerStream : public quic::QuicCryptoServerStream,
                                   public ProofSourceDetailsSetter {
public:
  ~TestQuicCryptoServerStream() override = default;

  explicit TestQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                                      quic::QuicCompressedCertsCache* compressed_certs_cache,
                                      quic::QuicSession* session,
                                      quic::QuicCryptoServerStreamBase::Helper* helper)
      : quic::QuicCryptoServerStream(crypto_config, compressed_certs_cache, session, helper) {}

  bool encryption_established() const override { return true; }

  const EnvoyQuicProofSourceDetails* ProofSourceDetails() const override { return details_.get(); }

  void setProofSourceDetails(std::unique_ptr<EnvoyQuicProofSourceDetails> details) override {
    details_ = std::move(details);
  }

private:
  std::unique_ptr<EnvoyQuicProofSourceDetails> details_;
};

class TestEnvoyQuicTlsServerHandshaker : public quic::TlsServerHandshaker,
                                         public ProofSourceDetailsSetter {
public:
  ~TestEnvoyQuicTlsServerHandshaker() override = default;

  TestEnvoyQuicTlsServerHandshaker(quic::QuicSession* session,
                                   const quic::QuicCryptoServerConfig& crypto_config)
      : quic::TlsServerHandshaker(session, &crypto_config),
        params_(new quic::QuicCryptoNegotiatedParameters) {
    params_->cipher_suite = 1;
  }

  bool encryption_established() const override { return true; }
  const EnvoyQuicProofSourceDetails* ProofSourceDetails() const override { return details_.get(); }
  void setProofSourceDetails(std::unique_ptr<EnvoyQuicProofSourceDetails> details) override {
    details_ = std::move(details);
  }
  const quic::QuicCryptoNegotiatedParameters& crypto_negotiated_params() const override {
    return *params_;
  }

private:
  std::unique_ptr<EnvoyQuicProofSourceDetails> details_;
  quiche::QuicheReferenceCountedPointer<quic::QuicCryptoNegotiatedParameters> params_;
};

class EnvoyQuicTestCryptoServerStreamFactory : public EnvoyQuicCryptoServerStreamFactoryInterface {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }
  std::string name() const override { return "quic.test_crypto_server_stream"; }

  std::unique_ptr<quic::QuicCryptoServerStreamBase> createEnvoyQuicCryptoServerStream(
      const quic::QuicCryptoServerConfig* crypto_config,
      quic::QuicCompressedCertsCache* compressed_certs_cache, quic::QuicSession* session,
      quic::QuicCryptoServerStreamBase::Helper* helper,
      OptRef<const Network::DownstreamTransportSocketFactory> /*transport_socket_factory*/,
      Event::Dispatcher& /*dispatcher*/) override {
    switch (session->connection()->version().handshake_protocol) {
    case quic::PROTOCOL_QUIC_CRYPTO:
      return std::make_unique<TestQuicCryptoServerStream>(crypto_config, compressed_certs_cache,
                                                          session, helper);
    case quic::PROTOCOL_TLS1_3:
      return std::make_unique<TestEnvoyQuicTlsServerHandshaker>(session, *crypto_config);
    case quic::PROTOCOL_UNSUPPORTED:
      ASSERT(false, "Unknown handshake protocol");
    }
    return nullptr;
  }
};

class FuzzDecrypter : public quic::QuicDecrypter {
public:
  explicit FuzzDecrypter() = default;
  FuzzDecrypter(const FuzzDecrypter&) = delete;
  FuzzDecrypter& operator=(const FuzzDecrypter&) = delete;
  ~FuzzDecrypter() override = default;

  bool SetKey(absl::string_view key) override { return key.empty(); };
  bool SetNoncePrefix(absl::string_view nonce_prefix) override { return nonce_prefix.empty(); };
  bool SetIV(absl::string_view iv) override { return iv.empty(); };
  bool SetPreliminaryKey(absl::string_view) override { return false; };
  bool SetHeaderProtectionKey(absl::string_view key) override { return key.empty(); };
  bool SetDiversificationNonce(const quic::DiversificationNonce&) override { return true; };
  bool DecryptPacket(uint64_t, absl::string_view, absl::string_view ciphertext, char* output,
                     size_t* output_length, size_t max_output_length) override {
    ASSERT(ciphertext.length() <= max_output_length);
    memcpy(output, ciphertext.data(), ciphertext.length());
    *output_length = ciphertext.length();
    return true;
  };
  std::string GenerateHeaderProtectionMask(quic::QuicDataReader*) override {
    return std::string(5, 0);
  };
  size_t GetKeySize() const override { return 0; };
  size_t GetNoncePrefixSize() const override { return 0; };
  size_t GetIVSize() const override { return 0; };
  absl::string_view GetKey() const override { return {}; };
  absl::string_view GetNoncePrefix() const override { return {}; };
  uint32_t cipher_id() const override { return 0; };
  quic::QuicPacketCount GetIntegrityLimit() const override {
    return std::numeric_limits<quic::QuicPacketCount>::max();
  }
};

envoy::config::core::v3::Http3ProtocolOptions http3Settings() { return {}; }

QuicDispatcherStats generateStats(Stats::Scope& store) {
  return {QUIC_DISPATCHER_STATS(POOL_COUNTER_PREFIX(store, "quic.dispatcher"))};
}

class FuzzDispatcher : public quic::QuicDispatcher {
public:
  FuzzDispatcher(quic::ParsedQuicVersion quic_version, quic::QuicVersionManager* version_manager,
                 std::unique_ptr<quic::QuicConnectionHelperInterface> connection_helper,
                 std::unique_ptr<quic::QuicAlarmFactory> alarm_factory,
                 Event::Dispatcher& dispatcher)
      : quic::QuicDispatcher(
            &quic_config_, &crypto_config_, version_manager, std::move(connection_helper),
            std::make_unique<EnvoyQuicCryptoServerStreamHelper>(), std::move(alarm_factory),
            quic::kQuicDefaultConnectionIdLength, generator_),
        listener_config_(&mock_listener_config_),
        quic_stats_(generateStats(listener_config_->listenerScope())), dispatcher_(dispatcher),
        http3_options_(http3Settings()), quic_version_(quic_version),
        packetizer_(quic_version_, helper()),
        crypto_config_(quic::QuicCryptoServerConfig::TESTING, quic::QuicRandom::GetInstance(),
                       std::make_unique<TestProofSource>(), quic::KeyExchangeSource::Default()),
        peer_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        12345)),
        self_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        54321)),
        cli_addr_(peer_addr_->sockAddr(), peer_addr_->sockAddrLen()),
        srv_addr_(self_addr_->sockAddr(), self_addr_->sockAddrLen()),
        quic_stat_names_(listener_config_->listenerScope().symbolTable()),
        http3_stats_({ALL_HTTP3_CODEC_STATS(
            POOL_COUNTER_PREFIX(listener_config_->listenerScope(), "http3."),
            POOL_GAUGE_PREFIX(listener_config_->listenerScope(), "http3."))}),
        connection_stats_({QUIC_CONNECTION_STATS(
            POOL_COUNTER_PREFIX(listener_config_->listenerScope(), "quic.connection"))}) {
    ON_CALL(http_connection_callbacks_, newStream(_, _))
        .WillByDefault(Invoke([&](Http::ResponseEncoder&, bool) -> Http::RequestDecoder& {
          return orphan_request_decoder_;
        }));
    auto writer = new testing::NiceMock<quic::test::MockPacketWriter>();
    ON_CALL(*writer, WritePacket(_, _, _, _, _))
        .WillByDefault(testing::Return(quic::WriteResult(quic::WRITE_STATUS_OK, 0)));
    InitializeWithWriter(writer);
  }

  void fuzzQuic(const QuicH3FuzzCase& input) {
    quic::QuicReceivedPacket first_packet(nullptr, 0, helper()->GetClock()->Now());
    quic::ParsedClientHello chlo;
    quic::ReceivedPacketInfo chlo_packet_info(srv_addr_, cli_addr_, first_packet);
    chlo_packet_info.destination_connection_id = quic::test::TestConnectionId();
    chlo_packet_info.version = quic_version_;
    SetQuicFlag(quic_allow_chlo_buffering, false);
    ProcessChlo(chlo, &chlo_packet_info);

    packetizer_.serializePackets(input);
    for (size_t i = 0; i < kMaxNumPackets; i++) {
      const char* payload = quic_packets[i];
      size_t size = quic_packet_sizes[i];
      if (size == 0) {
        continue;
      }
      auto receipt_time = helper()->GetClock()->Now();
      quic::QuicReceivedPacket p(payload, size, receipt_time, false);
      ProcessPacket(srv_addr_, cli_addr_, p);
    }
    packetizer_.reset();
    Shutdown();
  }
  void OnConnectionClosed(quic::QuicConnectionId connection_id, quic::QuicErrorCode error,
                          const std::string& error_details,
                          quic::ConnectionCloseSource source) override {
    quic::QuicDispatcher::OnConnectionClosed(connection_id, error, error_details, source);
  }
  quic::QuicTimeWaitListManager* CreateQuicTimeWaitListManager() override {
    return new EnvoyQuicTimeWaitListManager(writer(), this, helper()->GetClock(), alarm_factory(),
                                            quic_stats_);
  }

  void closeConnectionsWithFilterChain(const Network::FilterChain*) {}

  void updateListenerConfig(Network::ListenerConfig& new_listener_config) {
    listener_config_ = &new_listener_config;
  }

protected:
  std::unique_ptr<quic::QuicSession>
  CreateQuicSession(quic::QuicConnectionId server_connection_id,
                    const quic::QuicSocketAddress& self_address,
                    const quic::QuicSocketAddress& peer_address, absl::string_view /*alpn*/,
                    const quic::ParsedQuicVersion& version,
                    const quic::ParsedClientHello& /*parsed_chlo*/) override {

    auto connection_socket = Quic::createConnectionSocket(peer_addr_, self_addr_, nullptr);
    auto connection = std::make_unique<EnvoyQuicServerConnection>(
        server_connection_id, self_address, peer_address, *helper(), *alarm_factory(), writer(),
        false, quic::ParsedQuicVersionVector{quic_version_}, std::move(connection_socket),
        connection_id_generator());

    auto decrypter = std::make_unique<FuzzDecrypter>();
    auto encrypter = std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_CLIENT);
    connection->InstallDecrypter(quic::EncryptionLevel::ENCRYPTION_FORWARD_SECURE,
                                 std::move(decrypter));
    connection->SetEncrypter(quic::EncryptionLevel::ENCRYPTION_FORWARD_SECURE,
                             std::move(encrypter));

    connection->SetDefaultEncryptionLevel(quic::EncryptionLevel::ENCRYPTION_FORWARD_SECURE);

    auto stream_info = std::make_unique<StreamInfo::StreamInfoImpl>(
        dispatcher_.timeSource(),
        connection->connectionSocket()->connectionInfoProviderSharedPtr());
    auto session = std::make_unique<EnvoyQuicServerSession>(
        quic_config_, quic::ParsedQuicVersionVector{version}, std::move(connection), this,
        &crypto_stream_helper_, &crypto_config_, &compressed_certs_cache_, dispatcher_,
        quic::kDefaultFlowControlSendWindow * 1.5, quic_stat_names_,
        listener_config_->listenerScope(), crypto_stream_factory_, std::move(stream_info),
        connection_stats_);
    session->Initialize();
    session->setHeadersWithUnderscoreAction(envoy::config::core::v3::HttpProtocolOptions::ALLOW);
    session->setHttp3Options(http3_options_);
    session->setCodecStats(http3_stats_);
    session->setHttpConnectionCallbacks(http_connection_callbacks_);
    session->setMaxIncomingHeadersCount(100);
    session->set_max_inbound_header_list_size(64 * 1024u);
    setQuicConfigWithDefaultValues(session->config());
    session->OnConfigNegotiated();
    return session;
  }

private:
  NiceMock<Network::MockListenerConfig> mock_listener_config_;
  Network::ListenerConfig* listener_config_{};
  QuicDispatcherStats quic_stats_;
  Event::Dispatcher& dispatcher_;

  envoy::config::core::v3::Http3ProtocolOptions http3_options_;
  quic::ParsedQuicVersion quic_version_;
  QuicPacketizer packetizer_;
  quic::QuicCryptoServerConfig crypto_config_;
  NiceMock<quic::test::MockQuicCryptoServerStreamHelper> crypto_stream_helper_;
  Network::Address::InstanceConstSharedPtr peer_addr_;
  Network::Address::InstanceConstSharedPtr self_addr_;
  quic::QuicSocketAddress cli_addr_;
  quic::QuicSocketAddress srv_addr_;
  QuicStatNames quic_stat_names_;
  Http::Http3::CodecStats http3_stats_;
  QuicConnectionStats connection_stats_;

  quic::QuicConfig quic_config_;
  quic::DeterministicConnectionIdGenerator generator_{quic::kQuicDefaultConnectionIdLength};
  quic::QuicCompressedCertsCache compressed_certs_cache_{100};
  EnvoyQuicTestCryptoServerStreamFactory crypto_stream_factory_;

  Http::MockServerConnectionCallbacks http_connection_callbacks_;
  NiceMock<Http::MockRequestDecoder> orphan_request_decoder_;
};

struct Harness {
  Harness(quic::ParsedQuicVersion quic_version)
      : quic_version_(quic_version), version_manager(quic::CurrentSupportedHttp3Versions()) {
    api = Api::createApiForTest();
    dispatcher = api->allocateDispatcher("envoy_quic_h3_fuzzer_thread");
    auto connection_helper = std::unique_ptr<quic::QuicConnectionHelperInterface>(
        new EnvoyQuicConnectionHelper(*dispatcher.get()));
    auto alarm_factory = std::unique_ptr<quic::QuicAlarmFactory>(
        new EnvoyQuicAlarmFactory(*dispatcher.get(), *connection_helper->GetClock()));
    fuzz_dispatcher = std::make_unique<FuzzDispatcher>(quic_version_, &version_manager,
                                                       std::move(connection_helper),
                                                       std::move(alarm_factory), *dispatcher.get());
  }
  quic::ParsedQuicVersion quic_version_;
  Api::ApiPtr api;
  Event::DispatcherPtr dispatcher;
  std::unique_ptr<FuzzDispatcher> fuzz_dispatcher;
  quic::QuicVersionManager version_manager;
};

std::unique_ptr<Harness> harness;
static void resetHarness() { harness = nullptr; };
DEFINE_PROTO_FUZZER(const test::common::quic::QuicH3FuzzCase& input) {
  if (harness == nullptr) {
    quiche::setVerbosityLogThreshold(0);
    harness = std::make_unique<Harness>(quic::CurrentSupportedHttp3Versions()[0]);
    atexit(resetHarness);
  }
  harness->fuzz_dispatcher->fuzzQuic(input);
  fflush(stdout);
}

} // namespace Quic
} // namespace Envoy
