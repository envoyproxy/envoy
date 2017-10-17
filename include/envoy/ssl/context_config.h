#pragma once

#include <array>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

/**
 * Supplies the configuration for an SSL context.
 */
class ContextConfig {
public:
  virtual ~ContextConfig() {}

  /**
   * The list of supported protocols exposed via ALPN. Client connections will send these
   * protocols to the server. Server connections will use these protocols to select the next
   * protocol if the client supports ALPN.
   */
  virtual const std::string& alpnProtocols() const PURE;

  /**
   * The alternate list of ALPN protocols served via kill switch. @see alpnProtocols().
   */
  virtual const std::string& altAlpnProtocols() const PURE;

  /**
   * The ':' delimited list of supported cipher suites
   */
  virtual const std::string& cipherSuites() const PURE;

  /**
   * The ':' delimited list of supported ECDH curves.
   */
  virtual const std::string& ecdhCurves() const PURE;

  /**
   * @return The CA certificate file to use for peer validation.
   */
  virtual const std::string& caCertFile() const PURE;

  /**
   * @return The certificate chain file used to identify the local side.
   */
  virtual const std::string& certChainFile() const PURE;

  /**
   * @return The private key chain file used to identify the local side.
   */
  virtual const std::string& privateKeyFile() const PURE;

  /**
   * @return The subject alt names to be verified, if enabled. Otherwise, ""
   */
  virtual const std::vector<std::string>& verifySubjectAltNameList() const PURE;

  /**
   * @return The hex string representation of the certificate hash to be verified, if enabled.
   * Otherwise, ""
   */
  virtual const std::string& verifyCertificateHash() const PURE;
};

class ClientContextConfig : public virtual ContextConfig {
public:
  /**
   * @return The server name indication if it's set and ssl enabled
   * Otherwise, ""
   */
  virtual const std::string& serverNameIndication() const PURE;
};

class ServerContextConfig : public virtual ContextConfig {
public:
  struct SessionTicketKey {
    std::array<uint8_t, 16> name_;         // 16 == SSL_TICKET_KEY_NAME_LEN
    std::array<uint8_t, 32> hmac_key_;     // 32 == SHA256_DIGEST_LENGTH
    std::array<uint8_t, 256 / 8> aes_key_; // AES256 key size, in bytes
  };

  /**
   * @return True if client certificate is required, false otherwise.
   */
  virtual bool requireClientCertificate() const PURE;

  /**
   * @return The keys to use for encrypting and decrypting session tickets.
   * The first element is used for encrypting new tickets, and all elements
   * are candidates for decrypting received tickets.
   */
  virtual const std::vector<SessionTicketKey>& sessionTicketKeys() const PURE;
};

} // namespace Ssl
} // namespace Envoy
