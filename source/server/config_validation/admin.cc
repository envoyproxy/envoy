#include "server/config_validation/admin.h"

namespace Envoy {
namespace Server {

bool ValidationAdmin::addHandler(const std::string&, const std::string&, HandlerCb, bool, bool) {
  return false;
};

bool ValidationAdmin::removeHandler(const std::string&) { return false; };

const Network::Socket& ValidationAdmin::socket() { NOT_IMPLEMENTED; };

ConfigTracker& ValidationAdmin::getConfigTracker() { return config_tracker_; };

} // namespace Server
} // namespace Envoy
