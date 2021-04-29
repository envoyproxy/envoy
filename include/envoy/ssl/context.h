#pragma once

#include <memory>
#include <string>

#include "envoy/admin/v3/certs.pb.h"
#include "envoy/common/pure.h"

#include "absl/types/optional.h"

struct x509_st;
struct stack_st_X509;

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
   * @return the number of days in this context until the next certificate will expire
   */
  virtual size_t daysUntilFirstCertExpires() const PURE;

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

  /*
   * Attempts to verify the certificate chain.
   * @param leaf_cert the leaf certificate to be verified.
   * @param intermediates the intermediate certificates, if present.
   * @param error_details if an error is encountered, a human readable explanation will be stored
   * here.
   * @return true if the certificate chain is verified, false otherwise.
   */
  virtual bool verifyCertChain(x509_st& /*leaf_cert*/, stack_st_X509& /*intermediates*/,
                               std::string& /*error_details*/) {
    return false;
  }
};
using ContextSharedPtr = std::shared_ptr<Context>;

class ClientContext : public virtual Context {};
using ClientContextSharedPtr = std::shared_ptr<ClientContext>;

class ServerContext : public virtual Context {};
using ServerContextSharedPtr = std::shared_ptr<ServerContext>;

} // namespace Ssl
} // namespace Envoy
