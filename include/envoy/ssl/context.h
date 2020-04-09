#pragma once

#include <memory>
#include <string>

#include "envoy/admin/v3/certs.pb.h"
#include "envoy/common/pure.h"

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
};
using ContextSharedPtr = std::shared_ptr<Context>;

class ClientContext : public virtual Context {};
using ClientContextSharedPtr = std::shared_ptr<ClientContext>;

class ServerContext : public virtual Context {};
using ServerContextSharedPtr = std::shared_ptr<ServerContext>;

} // namespace Ssl
} // namespace Envoy
