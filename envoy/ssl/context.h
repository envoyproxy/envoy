#pragma once

#include <memory>
#include <string>

#include "envoy/admin/v3/certs.pb.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Ssl {

using CertificateDetailsPtr = std::unique_ptr<envoy::admin::v3::CertificateDetails>;

/**
 * SSL Context is used as a template for SSL connection configuration.
 */
class Context {
public:
  virtual ~Context() = default;

  /**
   * @return the number of days in this context until the next certificate will expire, the value is
   * set when not expired.
   */
  virtual absl::optional<uint32_t> daysUntilFirstCertExpires() const PURE;

  /**
   * @return certificate details conforming to proto admin.v2alpha.certs.
   */
  virtual CertificateDetailsPtr getCaCertInformation() const PURE;

  /**
   * @return certificate details conforming to proto admin.v2alpha.certs.
   */
  virtual std::vector<CertificateDetailsPtr> getCertChainInformation() const PURE;

  /**
   * @return the number of seconds in this context until the next OCSP response will
   * expire, or `absl::nullopt` if no OCSP responses exist.
   */
  virtual absl::optional<uint64_t> secondsUntilFirstOcspResponseExpires() const PURE;
};
using ContextSharedPtr = std::shared_ptr<Context>;

class ClientContext : public virtual Context {};
using ClientContextSharedPtr = std::shared_ptr<ClientContext>;

class ServerContext : public virtual Context {};
using ServerContextSharedPtr = std::shared_ptr<ServerContext>;

class OcspResponseWrapper {
public:
  virtual ~OcspResponseWrapper() = default;
  /**
   * @returns the seconds until this OCSP response expires.
   */
  virtual uint64_t secondsUntilExpiration() const PURE;

  /**
   * @return The beginning of the validity window for this response.
   */
  virtual Envoy::SystemTime getThisUpdate() const PURE;

  /**
   * The time at which this response is considered to expire. If
   * the underlying response does not have a value, then the current
   * time is returned.
   *
   * @return The end of the validity window for this response.
   */
  virtual Envoy::SystemTime getNextUpdate() const PURE;

  /**
   * Determines whether the OCSP response can no longer be considered valid.
   * This can be true if the nextUpdate field of the response has passed
   * or is not present, indicating that there is always more updated information
   * available.
   *
   * @returns bool if the OCSP response is expired.
   */
  virtual bool isExpired() PURE;

  /**
   * @return std::vector<uint8_t>& a reference to the underlying bytestring representation
   * of the OCSP response
   */
  virtual const std::vector<uint8_t>& rawBytes() const PURE;
};

} // namespace Ssl
} // namespace Envoy
