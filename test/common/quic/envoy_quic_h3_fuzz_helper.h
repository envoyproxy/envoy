#include <set>

#include "test/common/quic/envoy_quic_h3_fuzz.pb.h"

#include "quiche/quic/core/crypto/null_decrypter.h"
#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_versions.h"

namespace Envoy {
namespace Quic {

// This class serializes structured protobuf `HTTP/3` messages to bytes as they
// would be sent over the wire.
class H3Packetizer {
public:
  H3Packetizer(std::set<uint32_t>& streams) : open_h3_streams_(streams){};
  size_t serialize(char* buffer, size_t buffer_len, bool unidirectional, uint32_t type, uint32_t id,
                   const test::common::quic::H3Frame& h3frame);

private:
  std::set<uint32_t>& open_h3_streams_;
};

// This class serializes structured protobuf `QUIC + HTTP/3` messages to bytes as they
// would be sent over the wire.
class QuicPacketizer {
public:
  QuicPacketizer(const quic::ParsedQuicVersion& quic_version,
                 quic::QuicConnectionHelperInterface* connection_helper);
  void serializePackets(const test::common::quic::QuicH3FuzzCase& input);
  void foreach (std::function<void(const char*, size_t)> cb);
  void reset();

private:
  void serializePacket(const test::common::quic::QuicFrame& frame);

  quic::ParsedQuicVersion quic_version_;
  quic::QuicConnectionHelperInterface* connection_helper_;
  quic::QuicPacketNumber packet_number_;

  quic::QuicConnectionId destination_connection_id_;
  quic::QuicFramer framer_;

  H3Packetizer h3packetizer_;
  std::set<uint32_t> open_h3_streams_;

  char quic_packets_[10][1460];
  size_t quic_packet_sizes_[10] = {0};
  size_t idx_{0};
};

// The following two classes handle the encryption and decryption in the fuzzer
// and just pass the plain or cipher text as is. The classes override
// the `EncryptPacket` method, because the `Null{En,De}crypter` calculates
// a hash of the data for each packet, which is unnecessary in fuzzing.
class FuzzEncrypter : public quic::NullEncrypter {
public:
  explicit FuzzEncrypter(quic::Perspective perspective) : NullEncrypter(perspective){};
  bool EncryptPacket(uint64_t, absl::string_view, absl::string_view plaintext, char* output,
                     size_t* output_length, size_t max_output_length) {
    ASSERT(plaintext.length() <= max_output_length);
    memcpy(output, plaintext.data(), plaintext.length());
    *output_length = plaintext.length();
    return true;
  };
  size_t GetMaxPlaintextSize(size_t ciphertext_size) const { return ciphertext_size; }
  size_t GetCiphertextSize(size_t plaintext_size) const { return plaintext_size; }
};

class FuzzDecrypter : public quic::NullDecrypter {
public:
  explicit FuzzDecrypter(quic::Perspective perspective) : NullDecrypter(perspective){};
  bool DecryptPacket(uint64_t, absl::string_view, absl::string_view ciphertext, char* output,
                     size_t* output_length, size_t max_output_length) {
    ASSERT(ciphertext.length() <= max_output_length);
    memcpy(output, ciphertext.data(), ciphertext.length());
    *output_length = ciphertext.length();
    return true;
  };
};

} // namespace Quic
} // namespace Envoy
