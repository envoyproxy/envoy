#pragma once

#include "envoy/server/admin.h"

#include "common/common/assert.h"

#include "server/http/config_tracker_impl.h"

namespace Envoy {
namespace Server {

/**
 * Config-validation-only implementation Server::Admin. This implementation is
 * needed because Admin is referenced by components of the server that add and
 * remove handlers.
 */
class ValidationAdmin : public Admin {
public:
  bool addHandler(const std::string&, const std::string&, HandlerCb, bool, bool) override;
  bool removeHandler(const std::string&) override;
  const Network::Socket& socket() override;
  ConfigTracker& getConfigTracker() override;

private:
  ConfigTrackerImpl config_tracker_;
};

} // namespace Server
} // namespace Envoy
