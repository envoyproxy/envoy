#pragma once

#include <string>
#include <vector>

namespace Envoy {
namespace Tls {
namespace Test {

// Options for generateClientHello.
struct ClientHelloOptions {
  ClientHelloOptions& setSniName(const std::string& sni_name) {
    sni_name_ = sni_name;
    return *this;
  }

  ClientHelloOptions& setAlpn(const std::string& alpn) {
    alpn_ = alpn;
    return *this;
  }

  ClientHelloOptions& setCipherSuites(const std::string& cipher_suites) {
    cipher_suites_ = cipher_suites;
    return *this;
  }

  ClientHelloOptions& setEcdhCurves(const std::string& ecdh_curves) {
    ecdh_curves_ = ecdh_curves;
    return *this;
  }

  ClientHelloOptions& setNoTlsV1_2() {
    no_tls_v1_2_ = true;
    return *this;
  }

  // The name to include as a Server Name Indication. No SNI extension is added if sni_name is
  // empty.
  std::string sni_name_;
  // Protocol(s) list in the wire-format (i.e. 8-bit length-prefixed string) to advertise in
  // Application-Layer Protocol Negotiation. No ALPN is advertised if alpn is empty.
  std::string alpn_;
  // :-delimited cipher suites override if non-empty.
  std::string cipher_suites_;
  // :-delimited ECDH curves list override if non-empty.
  std::string ecdh_curves_;
  // Force TLS < v1.2.
  bool no_tls_v1_2_;
};

/**
 * Generate a TLS ClientHello in wire-format.
 */
std::vector<uint8_t> generateClientHello(const ClientHelloOptions& options);

} // namespace Test
} // namespace Tls
} // namespace Envoy
