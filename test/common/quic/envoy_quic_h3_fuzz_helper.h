#include "test/common/quic/envoy_quic_h3_fuzz.pb.h"
#include <set>

#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/core/quic_connection.h"

class H3Packetizer {
public:
  H3Packetizer(std::set<uint32_t> &streams) : open_h3_streams_(streams) {};
  size_t serialize(char *buffer, size_t buffer_len, bool unidirectional,
      uint32_t type, uint32_t id, const test::common::quic::H3Frame& h3frame);
  
private:
  std::set<uint32_t> &open_h3_streams_;
};

class QuicPacketizer {
public:
  QuicPacketizer(const quic::ParsedQuicVersion& quic_version,
                 quic::QuicConnectionHelperInterface* connection_helper);
  void serializePackets(const test::common::quic::QuicH3FuzzCase& input);
  void foreach(std::function<void(const char *, size_t)> cb);
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
  std::string GenerateHeaderProtectionMask(absl::string_view) override { return {5, 0}; };
  size_t GetMaxPlaintextSize(size_t ciphertext_size) const override { return ciphertext_size; }
  size_t GetCiphertextSize(size_t plaintext_size) const override { return plaintext_size; }
  absl::string_view GetKey() const override { return {}; };
  absl::string_view GetNoncePrefix() const override { return {}; };
  quic::QuicPacketCount GetConfidentialityLimit() const override {
    return std::numeric_limits<quic::QuicPacketCount>::max();
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
  std::string GenerateHeaderProtectionMask(quic::QuicDataReader*) override { return {5, 0}; };
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
