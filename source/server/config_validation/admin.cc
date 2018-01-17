#include "server/config_validation/admin.h"

namespace Envoy {
namespace Server {

bool ValidationAdmin::addHandler(const std::string&, const std::string&, HandlerCb, bool, bool) {
  return false;
};

bool ValidationAdmin::removeHandler(const std::string&) { return false; };

const Network::ListenSocket& ValidationAdmin::socket() { NOT_IMPLEMENTED; };

} // namespace Server
} // namespace Envoy
