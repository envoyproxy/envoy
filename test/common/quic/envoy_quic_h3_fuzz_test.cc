#include "quiche/common/quiche_data_writer.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "quiche/quic/core/crypto/null_decrypter.h"
#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/tls_server_handshaker.h"
#include "quiche/quic/core/quic_crypto_server_stream.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_server_connection.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/server_codec_impl.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/common/quic/test_utils.h"
#include "test/common/quic/test_proof_source.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/http/mocks.h"

#include "test/common/quic/envoy_quic_h3_fuzz.pb.h"

// Uncomment the following line to enable debug visitors
// #define __DEBUG_QUIC_H3_FUZZER

namespace Envoy {
namespace Quic {

const size_t kMaxNumPackets = 10;

const size_t kMaxQuicPacketSize = 1460;
static char quic_packets[kMaxNumPackets][kMaxQuicPacketSize];
static size_t quic_packet_sizes[kMaxNumPackets];

const size_t kMaxH3PacketSize = 1024;
static char h3_packet[kMaxH3PacketSize];

static uint64_t clamp_u64(uint64_t in) {
  return in & ((1ULL << 62) - 1);
}

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
    size_t serialize(bool unidirectional, uint32_t type, uint32_t id, const H3Frame &h3frame) {
      quiche::QuicheDataWriter dw(kMaxH3PacketSize, h3_packet);
      if (unidirectional) {
        if (open_h3_streams.find(id) == open_h3_streams.end()) {
          dw.WriteVarInt62(static_cast<uint64_t>(type));
          open_h3_streams.insert(id);
        }
      }
      switch(h3frame.frame_case()) {
        case H3Frame::kData: {
          auto f = h3frame.data();
          return buildRawFrame(dw, Type::Data, f.data());
        }
        case H3Frame::kHeaders: {
          auto f = h3frame.headers();
          spdy::Http2HeaderBlock headers;
          for (auto hdr : f.headers().headers()) {
            headers.AppendValueOrAddHeader(hdr.key(), hdr.value());
          }
          return buildRawFrame(dw, Type::Headers, encodeHeaders(headers));
        }
        case H3Frame::kCancelPush: {
          auto f = h3frame.cancel_push();
          return buildVarIntFrame(dw, Type::CancelPush, f.push_id());
        }
        case H3Frame::kSettings: {
          auto f = h3frame.settings();
          std::vector<std::pair<uint64_t, uint64_t>> values;
          for (auto setting : f.settings()) {
            values.push_back(std::make_pair<uint64_t, uint64_t>(
                  setting.identifier(),
                  setting.value()));
          }
          return buildSettingsFrame(dw, values);
        }
        case H3Frame::kPushPromise: {
          auto f = h3frame.push_promise();
          uint64_t push_id = f.push_id();
          spdy::Http2HeaderBlock headers;
          for (auto hdr : f.headers().headers())
            headers.AppendValueOrAddHeader(hdr.key(), hdr.value());
          return buildPushPromiseFrame(dw, push_id, encodeHeaders(headers));
        }
        case H3Frame::kGoAway: {
          auto f = h3frame.go_away();
          return buildVarIntFrame(dw, Type::GoAway, f.push_id());
        }
        case H3Frame::kMaxPushId: {
          auto f = h3frame.max_push_id();
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
        void OnDecoderStreamError(quic::QuicErrorCode, absl::string_view) override {};
    };

    static std::string encodeHeaders(const spdy::Http2HeaderBlock &headers)
    {
      static Delegate delegate;
      quic::QpackEncoder encoder(&delegate);
      return encoder.EncodeHeaderList(0, headers, nullptr);
    }

    size_t buildRawFrame(quiche::QuicheDataWriter &dw, Type type, const std::string &payload) {
      bool valid = true;
      valid &= dw.WriteVarInt62(static_cast<uint64_t>(type));
      valid &= dw.WriteStringPieceVarInt62(payload);
      return valid?dw.length():0;
    }

    size_t buildVarIntFrame(quiche::QuicheDataWriter &dw, Type type, uint64_t number) {
      bool valid = true;
      uint64_t s = quiche::QuicheDataWriter::GetVarInt62Len(clamp_u64(number));
      valid &= dw.WriteVarInt62(static_cast<uint64_t>(type));
      valid &= dw.WriteVarInt62(s);
      valid &= dw.WriteVarInt62(clamp_u64(number));
      return valid?dw.length():0;
    }

    size_t buildSettingsFrame(quiche::QuicheDataWriter &dw,
        std::vector<std::pair<uint64_t, uint64_t>> &settings) {
      bool valid = true;
      uint64_t slen = 0;
      for(auto pair : settings) {
        slen += quiche::QuicheDataWriter::GetVarInt62Len(clamp_u64(pair.first));
        slen += quiche::QuicheDataWriter::GetVarInt62Len(clamp_u64(pair.second));
      }
      valid &= dw.WriteVarInt62(static_cast<uint64_t>(Type::Settings));
      valid &= dw.WriteVarInt62(slen);
      for(auto pair : settings) {
        valid &= dw.WriteVarInt62(clamp_u64(pair.first));
        valid &= dw.WriteVarInt62(clamp_u64(pair.second));
      }
      return valid?dw.length():0;
    }

    size_t buildPushPromiseFrame(quiche::QuicheDataWriter &dw, uint64_t push_id,
        const std::string &headers) {
      bool valid = true;
      uint64_t s = quiche::QuicheDataWriter::GetVarInt62Len(clamp_u64(push_id));
      s += headers.size();

      valid &= dw.WriteVarInt62(static_cast<uint64_t>(Type::PushPromise));
      valid &= dw.WriteVarInt62(s);
      valid &= dw.WriteVarInt62(clamp_u64(push_id));
      valid &= dw.WriteBytes(headers.data(), headers.size());
      return valid?dw.length():0;
    }
};

class QuicPacketizer {
  public:
    QuicPacketizer(const quic::ParsedQuicVersion &quic_version,
        EnvoyQuicConnectionHelper &connection_helper)
      : quic_version_(quic_version),
        connection_helper_(connection_helper),
        packet_number_(0),
        idx_(0),
        destination_connection_id_(quic::test::TestConnectionId()) {
        for(size_t i = 0; i < kMaxNumPackets; i++) {
          quic_packet_sizes[i] = 0;
        }
    }

    void serializePackets(const QuicH3FuzzCase &input) {
      for(auto &quic_frame_or_junk : input.frames()) {
        if(idx_ >= kMaxNumPackets) return;
        if (quic_frame_or_junk.has_qframe()) {
          serializePacket(quic_frame_or_junk.qframe());
        } else if (quic_frame_or_junk.has_junk()) {
          const std::string &junk = quic_frame_or_junk.junk();
          size_t len = std::min(junk.size(), kMaxQuicPacketSize);
          std::memcpy(quic_packets[idx_], junk.data(), len);
          quic_packet_sizes[idx_] = len;
          idx_++;
        }
      }
    }

  private:
    void serializePacket(const QuicFrame &frame) {
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
      // Need to initialize header.packet_number
      header.packet_number = packet_number_;
      header.destination_connection_id = destination_connection_id_;
      header.source_connection_id = destination_connection_id_;
      packet_number_++;
      quic::QuicFrames frames;
#define PUSH(frame) frames.push_back(quic::QuicFrame(frame))
      switch (frame.frame_case()) {
        case QuicFrame::kPadding: {
          int padding = frame.padding().num_padding_bytes() & 0xff;
          if (padding == 0) padding++;
          PUSH(quic::QuicPaddingFrame(padding));
          } break;
        case QuicFrame::kStream: {
          auto stream = frame.stream();
          size_t len = 0;
          bool unidirectional = stream.unidirectional();
          uint32_t type = stream.type();
          uint32_t id = stream.id();
          bool fin = stream.fin();
          uint64_t offset = clamp_u64(stream.offset());
          if (stream.has_h3frame()) {
            auto f = stream.h3frame();
            H3Packetizer h3packetizer;
            len = h3packetizer.serialize(unidirectional, type, id, f);
            if(len == 0) return;
          } else if (stream.has_junk()) {
            auto junk = stream.junk();
            len = std::min(junk.size(), sizeof(h3_packet));
            memcpy(h3_packet, junk.data(), len);
          } else return;
          PUSH(quic::QuicStreamFrame(id, fin, offset, h3_packet, len));
          } break;
        case QuicFrame::kHandshakeDone: {
          auto f = frame.handshake_done();
          PUSH(quic::QuicHandshakeDoneFrame(f.control_frame_id()));
        } break;
        case QuicFrame::kCrypto: {
          auto f = frame.crypto();
          uint16_t len = std::min(f.data().size(), sizeof(h3_packet));
          memcpy(h3_packet, f.data().data(), len);
          crypto_frame = std::make_unique<quic::QuicCryptoFrame>(
              quic::EncryptionLevel::ENCRYPTION_INITIAL, clamp_u64(f.offset()),
              h3_packet, len);
          PUSH(crypto_frame.get());
          } break;
        case QuicFrame::kAck: {
          auto f = frame.ack();
          auto largest_acked = quic::QuicPacketNumber(clamp_u64(f.largest_acked()));
          ack_frame.largest_acked = largest_acked;
          ack_frame.packets.Add(largest_acked);
          if (f.has_ecn_counters()) {
            auto c = f.ecn_counters();
            ack_frame.ecn_counters = quic::QuicEcnCounts(clamp_u64(c.ect0()),
                clamp_u64(c.ect1()), clamp_u64(c.ce()));
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
          auto f = frame.ping();
          PUSH(quic::QuicPingFrame(f.control_frame_id()));
          } break;
        case QuicFrame::kRstStream: {
          auto f = frame.rst_stream();
          quic::QuicRstStreamErrorCode error_code =
            static_cast<quic::QuicRstStreamErrorCode>(f.error_code());
          reset_stream_frame = std::make_unique<quic::QuicRstStreamFrame>(
              f.control_frame_id(), f.stream_id(), error_code, clamp_u64(f.bytes_written()));
          PUSH(reset_stream_frame.get());
          } break;
        case QuicFrame::kConnectionClose: {
          auto f = frame.connection_close();
          quic::QuicErrorCode error_code = toErrorCode(f.error_code());
          quic::QuicIetfTransportErrorCodes ietf_error =
            static_cast<quic::QuicIetfTransportErrorCodes>(clamp_u64(f.ietf_error()));
          connection_close_frame = std::make_unique<quic::QuicConnectionCloseFrame>(
              quic_version_.transport_version, error_code, ietf_error, f.error_phrase(),
              clamp_u64(f.transport_close_frame_type()));
          PUSH(connection_close_frame.get());
          } break;
        case QuicFrame::kGoAway: {
          // not possible in IETF mode
          return;
          } break;
        case QuicFrame::kWindowUpdate: {
          auto f = frame.window_update();
          PUSH(quic::QuicWindowUpdateFrame(f.control_frame_id(), f.stream_id(), clamp_u64(f.max_data())));
          } break;
        case QuicFrame::kBlocked: {
          auto f = frame.blocked();
          PUSH(quic::QuicBlockedFrame(f.control_frame_id(), f.stream_id(), clamp_u64(f.offset())));
          } break;
        case QuicFrame::kNewConnectionId: {
          auto f = frame.new_connection_id();
          new_connection_id_frame.control_frame_id = f.control_frame_id();
          new_connection_id_frame.connection_id = toConnectionId(f.connection_id());
          new_connection_id_frame.stateless_reset_token =
            toStatelessResetToken(f.stateless_reset_token());
          new_connection_id_frame.sequence_number = clamp_u64(f.sequence_number());
          PUSH(&new_connection_id_frame);
          } break;
        case QuicFrame::kRetireConnectionId: {
          // no retire frame in IETF mode
          return;
          } break;
        case QuicFrame::kMaxStreams: {
          auto f = frame.max_streams();
          PUSH(quic::QuicMaxStreamsFrame(f.control_frame_id(), f.stream_count(),
                f.unidirectional()));
          } break;
        case QuicFrame::kStreamsBlocked: {
          auto f = frame.streams_blocked();
          PUSH(quic::QuicStreamsBlockedFrame(f.control_frame_id(), f.stream_count(),
                f.unidirectional()));
          } break;
        case QuicFrame::kPathResponse: {
          auto f = frame.path_response();
          PUSH(quic::QuicPathResponseFrame(f.control_frame_id(), toPathFrameBuffer(f.data())));
          } break;
        case QuicFrame::kPathChallenge: {
          auto f = frame.path_challenge();
          PUSH(quic::QuicPathChallengeFrame(f.control_frame_id(), toPathFrameBuffer(f.data())));
          } break;
        case QuicFrame::kStopSending: {
          auto f = frame.stop_sending();
          quic::QuicRstStreamErrorCode error_code =
            static_cast<quic::QuicRstStreamErrorCode>(f.error_code());
          PUSH(quic::QuicStopSendingFrame(f.control_frame_id(), f.stream_id(), error_code));
          } break;
        case QuicFrame::kMessageFrame: {
          auto f = frame.message_frame();
          size_t len = std::min(f.data().size(), sizeof(h3_packet));
          memcpy(h3_packet, f.data().data(), len);
          message_frame = std::make_unique<quic::QuicMessageFrame>(h3_packet, len);
          PUSH(message_frame.get());
          } break;
        case QuicFrame::kNewToken: {
          auto f = frame.new_token();
          size_t len = std::min(f.token().size(), sizeof(h3_packet));
          memcpy(h3_packet, f.token().data(), len);
          absl::string_view token(h3_packet, len);
          new_token = std::make_unique<quic::QuicNewTokenFrame>(f.control_frame_id(), token);
          PUSH(new_token.get());
          } break;
        case QuicFrame::kAckFrequency: {
          auto f = frame.ack_frequency();
          auto delta = quic::QuicTime::Delta::FromMilliseconds(clamp_u64(f.milliseconds()));
          ack_frequency = std::make_unique<quic::QuicAckFrequencyFrame>(f.control_frame_id(),
              clamp_u64(f.sequence_number()), clamp_u64(f.packet_tolerance()), delta);
          PUSH(ack_frequency.get());
          } break;
        default:
          return;
      }
      quic::QuicTime creation_time = connection_helper_.GetClock()->Now();
      quic::QuicFramer framer({quic_version_}, creation_time, quic::Perspective::IS_CLIENT, 8);
      quic_packet_sizes[idx_] = framer.BuildDataPacket(header, frames,
          quic_packets[idx_], kMaxQuicPacketSize,
          quic::EncryptionLevel::ENCRYPTION_INITIAL);
      idx_++;
    }

    static quic::QuicConnectionId toConnectionId(const std::string &data) {
      size_t size = std::min(data.size(), 16UL);
      return quic::QuicConnectionId(data.data(), size);
    }

    static quic::StatelessResetToken toStatelessResetToken(const std::string &data) {
      quic::StatelessResetToken token = {0};
      size_t to_copy = std::min(data.size(), token.size());
      const char *start = data.data();
      const char *end = start + to_copy;
      std::copy(start, end, token.begin());
      return token;
    }

    static quic::QuicPathFrameBuffer toPathFrameBuffer(const std::string &data) {
      quic::QuicPathFrameBuffer buffer = {0};
      size_t to_copy = std::min(data.size(), buffer.size());
      const char *start = data.data();
      const char *end = start + to_copy;
      std::copy(start, end, buffer.begin());
      return buffer;
    }

    static quic::QuicErrorCode toErrorCode(uint32_t) {
      return quic::QuicErrorCode::QUIC_NO_ERROR;
    }

    quic::ParsedQuicVersion quic_version_;
    EnvoyQuicConnectionHelper &connection_helper_;
    quic::QuicPacketNumber packet_number_;
    size_t idx_;

    // For fuzzing
    quic::QuicConnectionId destination_connection_id_;
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
    explicit FuzzDecrypter() {};
    FuzzDecrypter(const FuzzDecrypter&) = delete;
    FuzzDecrypter& operator=(const FuzzDecrypter&) = delete;
    ~FuzzDecrypter() override {};

    bool SetKey(absl::string_view key) override { return key.empty(); };
    bool SetNoncePrefix(absl::string_view nonce_prefix) override { return nonce_prefix.empty(); };
    bool SetIV(absl::string_view iv) override { return iv.empty(); };
    bool SetPreliminaryKey(absl::string_view) override { return false; };
    bool SetHeaderProtectionKey(absl::string_view key) override { return key.empty(); };
    bool SetDiversificationNonce(const quic::DiversificationNonce &) override { return true; };
    bool DecryptPacket(uint64_t, absl::string_view,
        absl::string_view ciphertext, char* output, size_t* output_length,
        size_t max_output_length) override {
      assert(ciphertext.length() <= max_output_length);
      memcpy(output, ciphertext.data(), ciphertext.length());
      *output_length = ciphertext.length();
      return true;
    };
    std::string GenerateHeaderProtectionMask(quic::QuicDataReader *) override {
      return std::string(5, 0);
    };
    size_t GetKeySize() const override { return 0; };
    size_t GetNoncePrefixSize() const override { return 0; };
    size_t GetIVSize() const override { return 0; };
    absl::string_view GetKey() const override { return absl::string_view(); };
    absl::string_view GetNoncePrefix() const override { return absl::string_view(); };
    uint32_t cipher_id() const override { return 0; };
    quic::QuicPacketCount GetIntegrityLimit() const override {
      return std::numeric_limits<quic::QuicPacketCount>::max();
    }
};

#ifdef __DEBUG_QUIC_H3_FUZZER
#define LOG_VISIT { printf("QUIC::%s\n", __FUNCTION__); fflush(stdout); }
#define LOG_VISIT_H3 { printf("HTTP3::%s\n", __FUNCTION__); fflush(stdout); }
class ConnectionDebugVisitor : public quic::QuicConnectionDebugVisitor
{
  public:
    // Called when a coalesced packet is successfully serialized.
     void OnCoalescedPacketSent(
        const quic::QuicCoalescedPacket& /*coalesced_packet*/, size_t /*length*/) LOG_VISIT

    // Called when a PING frame has been sent.
     void OnPingSent() LOG_VISIT

    // Called when a packet has been received, but before it is
    // validated or parsed.
     void OnPacketReceived(const quic::QuicSocketAddress& /*self_address*/,
                                  const quic::QuicSocketAddress& /*peer_address*/,
                                  const quic::QuicEncryptedPacket& /*packet*/) LOG_VISIT

    // Called when the unauthenticated portion of the header has been parsed.
     void OnUnauthenticatedHeader(const quic::QuicPacketHeader& /*header*/) LOG_VISIT

    // Called when a packet is received with a connection id that does not
    // match the ID of this connection.
     void OnIncorrectConnectionId(quic::QuicConnectionId /*connection_id*/) LOG_VISIT

    // Called when an undecryptable packet has been received. If |dropped| is
    // true, the packet has been dropped. Otherwise, the packet will be queued and
    // connection will attempt to process it later.
     void OnUndecryptablePacket(quic::EncryptionLevel /*decryption_level*/,
                                       bool /*dropped*/) LOG_VISIT

    // Called when attempting to process a previously undecryptable packet.
     void OnAttemptingToProcessUndecryptablePacket(
        quic::EncryptionLevel /*decryption_level*/) LOG_VISIT

    // Called when a duplicate packet has been received.
     void OnDuplicatePacket(quic::QuicPacketNumber /*packet_number*/) LOG_VISIT

    // Called when the protocol version on the received packet doensn't match
    // current protocol version of the connection.
     void OnProtocolVersionMismatch(quic::ParsedQuicVersion /*version*/) LOG_VISIT

    // Called when the complete header of a packet has been parsed.
     void OnPacketHeader(const quic::QuicPacketHeader& /*header*/,
                                quic::QuicTime /*receive_time*/,
                                quic::EncryptionLevel /*level*/) LOG_VISIT

    // Called when a StreamFrame has been parsed.
     void OnStreamFrame(const quic::QuicStreamFrame& /*frame*/) LOG_VISIT

    // Called when a CRYPTO frame containing handshake data is received.
     void OnCryptoFrame(const quic::QuicCryptoFrame& /*frame*/) LOG_VISIT

    // Called when a StopWaitingFrame has been parsed.
     void OnStopWaitingFrame(const quic::QuicStopWaitingFrame& /*frame*/) LOG_VISIT

    // Called when a QuicPaddingFrame has been parsed.
     void OnPaddingFrame(const quic::QuicPaddingFrame& /*frame*/) LOG_VISIT

    // Called when a Ping has been parsed.
     void OnPingFrame(const quic::QuicPingFrame& /*frame*/,
                             quic::QuicTime::Delta /*ping_received_delay*/) LOG_VISIT

    // Called when a GoAway has been parsed.
     void OnGoAwayFrame(const quic::QuicGoAwayFrame& /*frame*/) LOG_VISIT

    // Called when a RstStreamFrame has been parsed.
     void OnRstStreamFrame(const quic::QuicRstStreamFrame& /*frame*/) LOG_VISIT

    // Called when a ConnectionCloseFrame has been parsed. All forms
    // of CONNECTION CLOSE are handled, Google QUIC, IETF QUIC
    // CONNECTION CLOSE/Transport and IETF QUIC CONNECTION CLOSE/Application
     void OnConnectionCloseFrame(
        const quic::QuicConnectionCloseFrame& /*frame*/) LOG_VISIT

    // Called when a WindowUpdate has been parsed.
     void OnWindowUpdateFrame(const quic::QuicWindowUpdateFrame& /*frame*/,
                                     const quic::QuicTime& /*receive_time*/) LOG_VISIT

    // Called when a BlockedFrame has been parsed.
     void OnBlockedFrame(const quic::QuicBlockedFrame& /*frame*/) LOG_VISIT

    // Called when a NewConnectionIdFrame has been parsed.
     void OnNewConnectionIdFrame(
        const quic::QuicNewConnectionIdFrame& /*frame*/) LOG_VISIT

    // Called when a RetireConnectionIdFrame has been parsed.
     void OnRetireConnectionIdFrame(
        const quic::QuicRetireConnectionIdFrame& /*frame*/) LOG_VISIT

    // Called when a NewTokenFrame has been parsed.
     void OnNewTokenFrame(const quic::QuicNewTokenFrame& /*frame*/) LOG_VISIT

    // Called when a MessageFrame has been parsed.
     void OnMessageFrame(const quic::QuicMessageFrame& /*frame*/) LOG_VISIT

    // Called when a HandshakeDoneFrame has been parsed.
     void OnHandshakeDoneFrame(const quic::QuicHandshakeDoneFrame& /*frame*/) LOG_VISIT

    // Called when a public reset packet has been received.
     void OnPublicResetPacket(const quic::QuicPublicResetPacket& /*packet*/) LOG_VISIT

    // Called when a version negotiation packet has been received.
     void OnVersionNegotiationPacket(
        const quic::QuicVersionNegotiationPacket& /*packet*/) LOG_VISIT

    // Called when the connection is closed.
     void OnConnectionClosed(const quic::QuicConnectionCloseFrame& /*frame*/,
                                    quic::ConnectionCloseSource /*source*/) LOG_VISIT

    // Called when the version negotiation is successful.
     void OnSuccessfulVersionNegotiation(
        const quic::ParsedQuicVersion& /*version*/) LOG_VISIT

    // Called when a CachedNetworkParameters is sent to the client.
     void OnSendConnectionState(
        const quic::CachedNetworkParameters& /*cached_network_params*/) LOG_VISIT

    // Called when a CachedNetworkParameters are received from the client.
     void OnReceiveConnectionState(
        const quic::CachedNetworkParameters& /*cached_network_params*/) LOG_VISIT

    // Called when the connection parameters are set from the supplied
    // |config|.
     void OnSetFromConfig(const quic::QuicConfig& /*config*/) LOG_VISIT

    // Called when RTT may have changed, including when an RTT is read from
    // the config.
     void OnRttChanged(quic::QuicTime::Delta /*rtt*/) const LOG_VISIT

    // Called when a StopSendingFrame has been parsed.
     void OnStopSendingFrame(const quic::QuicStopSendingFrame& /*frame*/) LOG_VISIT

    // Called when a PathChallengeFrame has been parsed.
     void OnPathChallengeFrame(const quic::QuicPathChallengeFrame& /*frame*/) LOG_VISIT

    // Called when a PathResponseFrame has been parsed.
     void OnPathResponseFrame(const quic::QuicPathResponseFrame& /*frame*/) LOG_VISIT

    // Called when a StreamsBlockedFrame has been parsed.
     void OnStreamsBlockedFrame(const quic::QuicStreamsBlockedFrame& /*frame*/) LOG_VISIT

    // Called when a MaxStreamsFrame has been parsed.
     void OnMaxStreamsFrame(const quic::QuicMaxStreamsFrame& /*frame*/) LOG_VISIT

    // Called when an AckFrequencyFrame has been parsed.
     void OnAckFrequencyFrame(const quic::QuicAckFrequencyFrame& /*frame*/) LOG_VISIT

    // Called when |count| packet numbers have been skipped.
     void OnNPacketNumbersSkipped(quic::QuicPacketCount /*count*/,
                                         quic::QuicTime /*now*/) LOG_VISIT

    // Called when a packet is serialized but discarded (i.e. not sent).
     void OnPacketDiscarded(const quic::SerializedPacket& /*packet*/) LOG_VISIT

    // Called for QUIC+TLS versions when we send transport parameters.
     void OnTransportParametersSent(
        const quic::TransportParameters& /*transport_parameters*/) LOG_VISIT

    // Called for QUIC+TLS versions when we receive transport parameters.
     void OnTransportParametersReceived(
        const quic::TransportParameters& /*transport_parameters*/) LOG_VISIT

    // Called for QUIC+TLS versions when we resume cached transport parameters for
    // 0-RTT.
     void OnTransportParametersResumed(
        const quic::TransportParameters& /*transport_parameters*/) LOG_VISIT

    // Called for QUIC+TLS versions when 0-RTT is rejected.
     void OnZeroRttRejected(int /*reject_reason*/) LOG_VISIT

    // Called for QUIC+TLS versions when 0-RTT packet gets acked.
     void OnZeroRttPacketAcked() LOG_VISIT

    // Called on peer address change.
     void OnPeerAddressChange(quic::AddressChangeType /*type*/,
                                     quic::QuicTime::Delta /*connection_time*/) LOG_VISIT

    // Called after peer migration is validated.
     void OnPeerMigrationValidated(quic::QuicTime::Delta /*connection_time*/) LOG_VISIT
};
static ConnectionDebugVisitor con_debug_visitor;

class Http3DebugVisitor : public quic::Http3DebugVisitor {
  public:
    // Called when locally-initiated control stream is created.
    void OnControlStreamCreated(quic::QuicStreamId /*stream_id*/) LOG_VISIT_H3
    // Called when locally-initiated QPACK encoder stream is created.
    void OnQpackEncoderStreamCreated(quic::QuicStreamId /*stream_id*/) LOG_VISIT_H3
    // Called when locally-initiated QPACK decoder stream is created.
    void OnQpackDecoderStreamCreated(quic::QuicStreamId /*stream_id*/) LOG_VISIT_H3
    // Called when peer's control stream type is received.
    void OnPeerControlStreamCreated(quic::QuicStreamId /*stream_id*/) LOG_VISIT_H3
    // Called when peer's QPACK encoder stream type is received.
    void OnPeerQpackEncoderStreamCreated(quic::QuicStreamId /*stream_id*/) LOG_VISIT_H3
    // Called when peer's QPACK decoder stream type is received.
    void OnPeerQpackDecoderStreamCreated(quic::QuicStreamId /*stream_id*/) LOG_VISIT_H3

    // Incoming HTTP/3 frames in ALPS TLS extension.
    void OnSettingsFrameReceivedViaAlps(const quic::SettingsFrame& /*frame*/) LOG_VISIT_H3
    void OnAcceptChFrameReceivedViaAlps(const quic::AcceptChFrame& /*frame*/) LOG_VISIT_H3

    // Incoming HTTP/3 frames on the control stream.
    void OnSettingsFrameReceived(const quic::SettingsFrame& /*frame*/) LOG_VISIT_H3
    void OnGoAwayFrameReceived(const quic::GoAwayFrame& /*frame*/) LOG_VISIT_H3
    void OnPriorityUpdateFrameReceived(
        const quic::PriorityUpdateFrame& /*frame*/) LOG_VISIT_H3
    void OnAcceptChFrameReceived(const quic::AcceptChFrame& /*frame*/) LOG_VISIT_H3

    // Incoming HTTP/3 frames on request or push streams.
    void OnDataFrameReceived(quic::QuicStreamId /*stream_id*/,
                                     quic::QuicByteCount /*payload_length*/) LOG_VISIT_H3
    void OnHeadersFrameReceived(
        quic::QuicStreamId /*stream_id*/,
        quic::QuicByteCount /*compressed_headers_length*/) LOG_VISIT_H3
    void OnHeadersDecoded(quic::QuicStreamId /*stream_id*/,
                                  quic::QuicHeaderList /*headers*/) LOG_VISIT_H3

    // Incoming HTTP/3 frames of unknown type on any stream.
    void OnUnknownFrameReceived(quic::QuicStreamId /*stream_id*/,
                                        uint64_t /*frame_type*/,
                                        quic::QuicByteCount /*payload_length*/) LOG_VISIT_H3

    // Outgoing HTTP/3 frames on the control stream.
    void OnSettingsFrameSent(const quic::SettingsFrame& /*frame*/) {};
    void OnGoAwayFrameSent(quic::QuicStreamId /*stream_id*/) {};
    void OnPriorityUpdateFrameSent(
        const quic::PriorityUpdateFrame& /*frame*/) {};

    // Outgoing HTTP/3 frames on request or push streams.
    void OnDataFrameSent(quic::QuicStreamId /*stream_id*/,
                                 quic::QuicByteCount /*payload_length*/) {};
    void OnHeadersFrameSent(
        quic::QuicStreamId /*stream_id*/,
        const spdy::Http2HeaderBlock& /*header_block*/) {};

    // 0-RTT related events.
    void OnSettingsFrameResumed(const quic::SettingsFrame& /*frame*/) {};
};
static Http3DebugVisitor http3_debug_visitor;
#endif

envoy::config::core::v3::Http3ProtocolOptions http3_settings() {
  return envoy::config::core::v3::Http3ProtocolOptions();
}

class QuicHarness {
  public:
    QuicHarness()
      : http3_options_(http3_settings()),
        api_(Api::createApiForTest()),
        dispatcher_(api_->allocateDispatcher("envoy_quic_h3_fuzzer_thread")),
        connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()),
        quic_version_(quic::CurrentSupportedHttp3Versions()[0]),
        crypto_config_(quic::QuicCryptoServerConfig::TESTING, quic::QuicRandom::GetInstance(),
            std::make_unique<TestProofSource>(), quic::KeyExchangeSource::Default()),
        peer_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
              12345)),
        self_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
              54321)),
        cli_addr_(peer_addr_->sockAddr(), peer_addr_->sockAddrLen()),
        srv_addr_(self_addr_->sockAddr(), self_addr_->sockAddrLen()),
        quic_stat_names_(listener_config_.listenerScope().symbolTable()),
        http3_stats_({ALL_HTTP3_CODEC_STATS(
            POOL_COUNTER_PREFIX(listener_config_.listenerScope(), "http3."),
            POOL_GAUGE_PREFIX(listener_config_.listenerScope(), "http3."))})
    {
      ON_CALL(http_connection_callbacks_, newStream(_,_))
        .WillByDefault(Invoke([&](Http::ResponseEncoder&,bool) -> Http::RequestDecoder& {
              return orphan_request_decoder_;
              }));
    }

    void fuzzQuic(const QuicH3FuzzCase &input) {
      auto connection = std::make_unique<EnvoyQuicServerConnection>(
          quic::test::TestConnectionId(),
          srv_addr_, cli_addr_,
          connection_helper_, alarm_factory_,
          &writer_, false,
          quic::ParsedQuicVersionVector{quic_version_},
          Quic::createConnectionSocket(peer_addr_, self_addr_, nullptr),
          connection_id_generator_);

      EnvoyQuicServerConnection *pconnection = connection.get();

      auto decrypter = std::make_unique<FuzzDecrypter>();
      auto encrypter = std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_SERVER);
      connection->InstallDecrypter(quic::EncryptionLevel::ENCRYPTION_FORWARD_SECURE,
          std::move(decrypter));
      connection->SetEncrypter(quic::EncryptionLevel::ENCRYPTION_FORWARD_SECURE,
          std::move(encrypter));

      connection->SetDefaultEncryptionLevel(quic::EncryptionLevel::ENCRYPTION_FORWARD_SECURE);

      auto stream_info = std::make_unique<StreamInfo::StreamInfoImpl>(
          dispatcher_->timeSource(),
          connection->connectionSocket()->connectionInfoProviderSharedPtr());
      session_ = std::make_unique<EnvoyQuicServerSession>(
          quic_config_,
          quic::ParsedQuicVersionVector{quic_version_},
          std::move(connection),
          nullptr,
          &crypto_stream_helper_, &crypto_config_, &compressed_certs_cache_,
          *dispatcher_,
          quic::kDefaultFlowControlSendWindow * 1.5,
          quic_stat_names_, listener_config_.listenerScope(),
          crypto_stream_factory_, std::move(stream_info));
      session_->Initialize();
      setQuicConfigWithDefaultValues(session_->config());
      session_->OnConfigNegotiated();
#ifdef __DEBUG_QUIC_H3_FUZZER
      session_->set_debug_visitor(&http3_debug_visitor);
      pconnection->set_debug_visitor(&con_debug_visitor);
#endif

      auto server = std::make_unique<QuicHttpServerConnectionImpl>(
          *session_.get(), http_connection_callbacks_, http3_stats_, http3_options_, 64 * 1024,
          100, envoy::config::core::v3::HttpProtocolOptions::ALLOW);


      QuicPacketizer packetizer(quic_version_, connection_helper_);
      packetizer.serializePackets(input);
      fflush(stdout);
      for(size_t i = 0; i < kMaxNumPackets; i++) {
        dispatchPacket(quic_packets[i], quic_packet_sizes[i]);
      }
      //session_->ProcessAllPendingStreams();
      pconnection->CloseConnection(quic::QUIC_NO_ERROR,
          quic::NO_IETF_QUIC_ERROR, "fuzzer done",
          quic::ConnectionCloseBehavior::SILENT_CLOSE);
      fflush(stdout);
    }

  private:
    void dispatchPacket(const char *payload, size_t size) {
      if (size == 0) return;
      auto receipt_time = connection_helper_.GetClock()->Now();
      quic::QuicReceivedPacket p(payload, size, receipt_time, false);
      session_->ProcessUdpPacket(srv_addr_, cli_addr_, p);
    }

  protected:
    envoy::config::core::v3::Http3ProtocolOptions http3_options_;
    Api::ApiPtr api_;
    Event::DispatcherPtr dispatcher_;
    EnvoyQuicConnectionHelper connection_helper_;
    EnvoyQuicAlarmFactory alarm_factory_;
    testing::NiceMock<quic::test::MockPacketWriter> writer_;
    quic::ParsedQuicVersion quic_version_;
    quic::QuicCryptoServerConfig crypto_config_;
    testing::NiceMock<quic::test::MockQuicCryptoServerStreamHelper> crypto_stream_helper_;
    Network::Address::InstanceConstSharedPtr peer_addr_;
    Network::Address::InstanceConstSharedPtr self_addr_;
    quic::QuicSocketAddress cli_addr_;
    quic::QuicSocketAddress srv_addr_;
    testing::NiceMock<Network::MockListenerConfig> listener_config_;
    QuicStatNames quic_stat_names_;
    Http::Http3::CodecStats http3_stats_;

    quic::QuicConfig quic_config_;
    quic::DeterministicConnectionIdGenerator connection_id_generator_{
      quic::kQuicDefaultConnectionIdLength};
    quic::QuicCompressedCertsCache compressed_certs_cache_{100};
    EnvoyQuicTestCryptoServerStreamFactory crypto_stream_factory_;


    std::unique_ptr<EnvoyQuicServerSession> session_;
    Http::MockServerConnectionCallbacks http_connection_callbacks_;
    quic::test::MockQuicConnectionVisitor mock_connection_visitor_;
    NiceMock<Http::MockRequestDecoder> orphan_request_decoder_;
};

static std::unique_ptr<QuicHarness> harness;
static void reset_harness() { harness = nullptr; };
DEFINE_PROTO_FUZZER(const test::common::quic::QuicH3FuzzCase &input) {
  if(harness == nullptr) {
    harness = std::make_unique<QuicHarness>();
    atexit(reset_harness);
  }
  harness->fuzzQuic(input);
}

} // namespace Quic
} // namespace Envoy
