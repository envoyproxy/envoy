#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/ssl/context.h"
#include "envoy/common/exception.h"

namespace Envoy {
namespace Secret {

/**
 * Secret contains certificate chain and private key
 */
class Secret {
 public:
  virtual ~Secret() {
  }

  /**
   * @return a name of the SDS secret
   */
  virtual const std::string& getName() PURE;

  /**
   * @return a string of certificate chain
   */
  virtual const std::string& getCertificateChain() PURE;
  /**
   * @return a string of private key
   */
  virtual const std::string& getPrivateKey() PURE;
};

typedef std::shared_ptr<Secret> SecretPtr;

typedef std::unordered_map<std::string, SecretPtr> SecretPtrMap;

typedef std::vector<SecretPtr> SecretPtrVector;

/**
 * Throws when the requested static secret is not available
 */
class EnvoyStaticSecretException : public EnvoyException {
 public:
  EnvoyStaticSecretException(const std::string& message)
      : EnvoyException(message) {
  }
};

/**
 * Throws when the requested dynamic secret is not available
 */
class EnvoyDynamicSecretNotReadyException : public EnvoyException {
 public:
  EnvoyDynamicSecretNotReadyException(const std::string& message)
      : EnvoyException(message) {
  }
};

}  // namespace Secret
}  // namespace Envoy
