#include "test/extensions/filters/listener/tls_inspector/tls_utility.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_split.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Tls {
namespace Test {

std::vector<uint8_t> generateClientHello(uint16_t tls_min_version, uint16_t tls_max_version,
                                         const std::string& sni_name, const std::string& alpn) {
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_with_buffers_method()));

  SSL_CTX_set_min_proto_version(ctx.get(), tls_min_version);
  SSL_CTX_set_max_proto_version(ctx.get(), tls_max_version);

  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));

  // Ownership of these is passed to *ssl
  BIO* in = BIO_new(BIO_s_mem());
  BIO* out = BIO_new(BIO_s_mem());
  SSL_set_bio(ssl.get(), in, out);

  SSL_set_connect_state(ssl.get());
  const char* const PREFERRED_CIPHERS = "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4";
  SSL_set_cipher_list(ssl.get(), PREFERRED_CIPHERS);
  if (!sni_name.empty()) {
    SSL_set_tlsext_host_name(ssl.get(), sni_name.c_str());
  }
  if (!alpn.empty()) {
    SSL_set_alpn_protos(ssl.get(), reinterpret_cast<const uint8_t*>(alpn.data()), alpn.size());
  }
  SSL_do_handshake(ssl.get());
  const uint8_t* data = nullptr;
  size_t data_len = 0;
  BIO_mem_contents(out, &data, &data_len);
  ASSERT(data_len > 0);
  std::vector<uint8_t> buf(data, data + data_len);
  return buf;
}

std::vector<uint8_t> generateClientHelloFromJA3Fingerprint(const std::string& ja3_fingerprint) {
  // fingerprint should have this format:
  //  SSLVersion,Cipher,SSLExtension,EllipticCurve,EllipticCurvePointFormat
  // Example:
  //   769,47-53-5-10-49161-49162-49171-49172-50-56-19-4,0-10-11,23-24-25,0
  std::vector<std::string> fingerprint = absl::StrSplit(ja3_fingerprint, ',');
  ASSERT(fingerprint.size() == 5);

  // version
  const uint16_t tls_version = std::stoi(fingerprint[0], nullptr);

  // ciphers
  std::vector<std::string> values = absl::StrSplit(fingerprint[1], '-');
  std::vector<uint8_t> ciphers;
  for (const std::string& v : values) {
    uint16_t cipher = std::stoi(v, nullptr);
    ciphers.push_back((cipher & 0xff00) >> 8);
    ciphers.push_back(cipher & 0xff);
  }

  // elliptic curves extension
  const uint16_t elliptic_curves_id = 0xa;
  values = absl::StrSplit(fingerprint[3], '-', absl::SkipEmpty());
  uint16_t length = values.size() * 2;
  uint16_t ext_length = length + 2;
  std::vector<uint8_t> elliptic_curves = {(elliptic_curves_id & 0xff00) >> 8,
                                          elliptic_curves_id & 0xff,
                                          static_cast<uint8_t>((ext_length & 0xff00) >> 8),
                                          static_cast<uint8_t>(ext_length & 0xff),
                                          static_cast<uint8_t>((length & 0xff00) >> 8),
                                          static_cast<uint8_t>(length & 0xff)};
  for (const std::string& v : values) {
    uint16_t elliptic_curve = std::stoi(v, nullptr);
    elliptic_curves.push_back((elliptic_curve & 0xff00) >> 8);
    elliptic_curves.push_back(elliptic_curve & 0xff);
  }

  // elliptic curve point formats extension
  const uint16_t elliptic_curve_point_formats_id = 0xb;
  values = absl::StrSplit(fingerprint[4], '-', absl::SkipEmpty());
  ext_length = values.size() + 1;
  std::vector<uint8_t> elliptic_curve_point_formats = {
      (elliptic_curve_point_formats_id & 0xff00) >> 8, elliptic_curve_point_formats_id & 0xff,
      static_cast<uint8_t>((ext_length & 0xff00) >> 8), static_cast<uint8_t>(ext_length & 0xff),
      static_cast<uint8_t>(values.size())};
  for (const std::string& v : values) {
    uint8_t elliptic_curve_point_format = std::stoi(v, nullptr);
    elliptic_curve_point_formats.push_back(elliptic_curve_point_format);
  }

  // server name extension
  const uint16_t server_name_id = 0x0;
  std::vector<uint8_t> server_name = {(server_name_id & 0xff00) >> 8, server_name_id & 0xff,
                                      // length
                                      0x00, 0x16,
                                      // list length
                                      0x00, 0x14,
                                      // hostname type
                                      0x00,
                                      // name length
                                      0x00, 0x11,
                                      // name (www.envoyproxy.io)
                                      'w', 'w', 'w', '.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o',
                                      'x', 'y', '.', 'i', 'o'};

  // signature algorithms extension
  const uint16_t signature_algorithms_id = 0xd;
  std::vector<uint8_t> signature_algorithms = {(signature_algorithms_id & 0xff00) >> 8,
                                               signature_algorithms_id & 0xff,
                                               // length
                                               0x00, 0x04,
                                               // list length
                                               0x00, 0x02,
                                               // algorithm
                                               0x04, 0x03};

  // ALPN extension
  const uint16_t alpn_id = 0x10;
  std::vector<uint8_t> alpn_extension = {(alpn_id & 0xff00) >> 8, alpn_id & 0xff,
                                         // length
                                         0x00, 0x0b,
                                         // list length
                                         0x00, 0x09,
                                         // protocol length
                                         0x08,
                                         // protocol name
                                         'H', 'T', 'T', 'P', '/', '1', '.', '1'};

  // extensions
  values = absl::StrSplit(fingerprint[2], '-', absl::SkipEmpty());
  std::vector<uint8_t> extensions;
  for (const std::string& v : values) {
    switch (std::stoi(v, nullptr)) {
    case elliptic_curves_id: {
      extensions.insert(std::end(extensions), std::begin(elliptic_curves),
                        std::end(elliptic_curves));
      break;
    }
    case elliptic_curve_point_formats_id: {
      extensions.insert(std::end(extensions), std::begin(elliptic_curve_point_formats),
                        std::end(elliptic_curve_point_formats));
      break;
    }
    case server_name_id: {
      extensions.insert(std::end(extensions), std::begin(server_name), std::end(server_name));
      break;
    }
    case signature_algorithms_id: {
      extensions.insert(std::end(extensions), std::begin(signature_algorithms),
                        std::end(signature_algorithms));
      break;
    }
    case alpn_id: {
      extensions.insert(std::end(extensions), std::begin(alpn_extension), std::end(alpn_extension));
      break;
    }
    default: {
      uint16_t extension_id = std::stoi(v, nullptr);
      extensions.push_back((extension_id & 0xff00) >> 8);
      extensions.push_back(extension_id & 0xff);
      extensions.push_back(0);
      extensions.push_back(0);
    }
    }
  }

  // client hello message
  std::vector<uint8_t> clienthello = {// client version
                                      static_cast<uint8_t>((tls_version & 0xff00) >> 8),
                                      static_cast<uint8_t>(tls_version & 0xff),
                                      // client random (32 bytes)
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      // session id
                                      0};
  // cipher suite length and ciphers
  uint16_t ciphers_length = ciphers.size();
  clienthello.push_back((ciphers_length & 0xff00) >> 8);
  clienthello.push_back(ciphers_length & 0xff);
  clienthello.insert(std::end(clienthello), std::begin(ciphers), std::end(ciphers));
  // compression methods
  clienthello.push_back(0x01);
  clienthello.push_back(0x00);
  // extension length and extensions
  uint16_t extensions_length = extensions.size();
  clienthello.push_back((extensions_length & 0xff00) >> 8);
  clienthello.push_back(extensions_length & 0xff);
  clienthello.insert(std::end(clienthello), std::begin(extensions), std::end(extensions));

  // headers
  uint32_t clienthello_bytes = clienthello.size();
  uint16_t handshake_bytes = clienthello.size() + 4;
  std::vector<uint8_t> clienthello_message = {
      // record header
      0x16, 0x03, 0x01,
      // handshake bytes
      static_cast<uint8_t>((handshake_bytes & 0xff00) >> 8),
      static_cast<uint8_t>(handshake_bytes & 0xff),
      // handshake header
      0x01,
      // client hello bytes
      static_cast<uint8_t>((clienthello_bytes & 0xff0000) >> 16),
      static_cast<uint8_t>((clienthello_bytes & 0xff00) >> 8),
      static_cast<uint8_t>(clienthello_bytes & 0xff)};
  clienthello_message.insert(std::end(clienthello_message), std::begin(clienthello),
                             std::end(clienthello));

  return clienthello_message;
}

} // namespace Test
} // namespace Tls
} // namespace Envoy
