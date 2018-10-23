#pragma once

#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/ssl/trusted_ca_config.h"

namespace Envoy {
namespace Ssl {

class TrustedCaConfigImpl : public TrustedCaConfig {
public:
  TrustedCaConfigImpl(const envoy::api::v2::core::DataSource& config);

  const std::string& caCert() const override { return ca_cert_; }
  const std::string& caCertPath() const override { return ca_cert_path_; }

private:
  const std::string ca_cert_;
  const std::string ca_cert_path_;
};
} // namespace Ssl
} // namespace Envoy
