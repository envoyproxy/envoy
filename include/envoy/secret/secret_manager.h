#pragma once

#include <string>
#include <sstream>
#include <iomanip>

#include <google/protobuf/util/json_util.h>

#include "envoy/secret/secret.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Secret {

/**
 * A manager for all static secrets
 */
class SecretManager {
 public:
  virtual ~SecretManager() {
  }

  /**
   * Add or update static secret
   *
   * @param secret Updated Secret
   * @return true when successful, otherwise returns false
   */
  virtual bool addOrUpdateStaticSecret(const SecretPtr secret) PURE;

  /**
   * @return the static secret for the given name
   */
  virtual SecretPtr getStaticSecret(const std::string& name) PURE;

};

}  // namespace Secret
}  // namespace Envoy

