#pragma once

#include <set>

#include "source/common/common/assert.h"

#include "test/common/quic/envoy_quic_h3_fuzz.pb.h"

#include "quiche/quic/core/crypto/null_decrypter.h"
#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_versions.h"

namespace Envoy {
namespace Quic {

// This class serializes structured protobuf `HTTP/3` messages to bytes as they
// would be sent over the wire.
class H3Serializer {
public:
  static constexpr size_t kMaxPacketSize = 1024;
  H3Serializer(std::set<uint32_t>& streams) : open_unidirectional_streams_(streams){};
  // This method serializes an `HTTP/3` frame given by `h3frame` to `std::string`.
  // If `unidirectional` is true, `type` will give the type of unidirectional
  // stream to be opened. `id` identifies an `HTTP/3` frame and is used to track
  // the whether the stream is already in flight.
  std::string serialize(bool unidirectional, uint32_t type, uint32_t id,
                        const test::common::quic::H3Frame& h3frame);

private:
  std::set<uint32_t>& open_unidirectional_streams_;
};

// This class serializes structured protobuf `QUIC + HTTP/3` messages to bytes as they
// would be sent over the wire.
class QuicPacketizer {
public:
  static constexpr size_t kMaxPacketSize = 1460;
  using QuicPacketPtr = std::unique_ptr<quic::QuicEncryptedPacket>;
  QuicPacketizer(const quic::ParsedQuicVersion& quic_version,
                 quic::QuicConnectionHelperInterface* connection_helper);
  std::vector<QuicPacketPtr> serializePackets(const test::common::quic::QuicH3FuzzCase& input);
  void reset();

private:
  QuicPacketPtr serializePacket(const test::common::quic::QuicFrame& frame);
  QuicPacketPtr serializeJunkPacket(const std::string& data);
  QuicPacketPtr serialize(quic::QuicFrame frame);
  QuicPacketPtr serializeStreamFrame(const test::common::quic::QuicStreamFrame& frame);
  QuicPacketPtr serializeNewTokenFrame(const test::common::quic::QuicNewTokenFrame& frame);
  QuicPacketPtr serializeMessageFrame(const test::common::quic::QuicMessageFrame& frame);
  QuicPacketPtr serializeCryptoFrame(const test::common::quic::QuicCryptoFrame& frame);
  QuicPacketPtr serializeAckFrame(const test::common::quic::QuicAckFrame& frame);
  QuicPacketPtr
  serializeNewConnectionIdFrame(const test::common::quic::QuicNewConnectionIdFrame& frame);

  quic::ParsedQuicVersion quic_version_;
  quic::QuicConnectionHelperInterface* connection_helper_;
  quic::QuicPacketNumber packet_number_;

  quic::QuicConnectionId destination_connection_id_;
  quic::QuicFramer framer_;

  H3Serializer h3serializer_;

  // Unidirectional streams are started by sending a variable-length integer
  // indicating the stream type (RFC9114, Sec. 6.2). H3Serializer serializes the
  // stream type into the stream payload upon seeing the stream for the first
  // time. This set tracks opened unidirectional streams in this flight. So that
  // multiple stream frames on the same stream will not cause the stream type to
  // be serialized again.
  std::set<uint32_t> open_unidirectional_streams_;
};

// The following two classes handle the encryption and decryption in the fuzzer
// and just pass the plain or cipher text as is. The classes override
// the `EncryptPacket` method, because the `Null{En,De}crypter` calculates
// a hash of the data for each packet, which is unnecessary in fuzzing.
class FuzzEncrypter : public quic::NullEncrypter {
public:
  explicit FuzzEncrypter(quic::Perspective perspective) : NullEncrypter(perspective){};
  bool EncryptPacket(uint64_t, absl::string_view, absl::string_view plaintext, char* output,
                     size_t* output_length, size_t max_output_length) override {
    ASSERT(plaintext.length() <= max_output_length);
    memcpy(output, plaintext.data(), plaintext.length());
    *output_length = plaintext.length();
    return true;
  };
  size_t GetMaxPlaintextSize(size_t ciphertext_size) const override { return ciphertext_size; }
  size_t GetCiphertextSize(size_t plaintext_size) const override { return plaintext_size; }
};

class FuzzDecrypter : public quic::NullDecrypter {
public:
  explicit FuzzDecrypter(quic::Perspective perspective) : NullDecrypter(perspective){};
  bool DecryptPacket(uint64_t, absl::string_view, absl::string_view ciphertext, char* output,
                     size_t* output_length, size_t max_output_length) override {
    ASSERT(ciphertext.length() <= max_output_length);
    memcpy(output, ciphertext.data(), ciphertext.length());
    *output_length = ciphertext.length();
    return true;
  };
};

} // namespace Quic
} // namespace Envoy
