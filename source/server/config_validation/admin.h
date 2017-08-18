#pragma once

#include "envoy/server/admin.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Server {

/**
 * Config-validation-only implementation Server::Admin
 */
class ValidationAdmin : public Admin {
public:
  ValidationAdmin() {}
  ~ValidationAdmin() {}

  bool addHandler(const std::string&, const std::string&, HandlerCb, bool) override;
  bool removeHandler(const std::string&) override;
  const Network::ListenSocket& socket() override;
};

} // namespace Server
} // namespace Envoy
