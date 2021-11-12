#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

enum class ClientValidationStatus { NotValidated, NoClientCertificate, Validated, Failed };

class SslExtendedSocketInfo {
public:
  virtual ~SslExtendedSocketInfo() = default;

  /**
   * Set the peer certificate validation status.
   **/
  virtual void setCertificateValidationStatus(ClientValidationStatus validated) PURE;

  /**
   * @return ClientValidationStatus The peer certificate validation status.
   **/
  virtual ClientValidationStatus certificateValidationStatus() const PURE;
};

} // namespace Ssl
} // namespace Envoy
