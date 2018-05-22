#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/ssl/context.h"

namespace Envoy {
namespace Secret {

/**
 * Secret contains certificate chain and private key
 */
class Secret {
public:
  virtual ~Secret() {}

  /**
   * @return a name of the SDS secret
   */
  virtual const std::string& name() const PURE;

  /**
   * @return a string of certificate chain
   */
  virtual const std::string& certificateChain() const PURE;
  /**
   * @return a string of private key
   */
  virtual const std::string& privateKey() const PURE;
};

typedef std::shared_ptr<Secret> SecretSharedPtr;

} // namespace Secret
} // namespace Envoy
