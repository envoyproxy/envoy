#include "source/server/config_validation/admin.h"

namespace Envoy {
namespace Server {

// Pretend that handler was added successfully.
bool ValidationAdmin::addStreamingHandler(const std::string&, const std::string&, GenRequestFn,
                                          bool, bool, const ParamDescriptorVec&) {
  return true;
}

bool ValidationAdmin::addHandler(const std::string&, const std::string&, HandlerCb, bool, bool,
                                 const ParamDescriptorVec&) {
  return true;
}

bool ValidationAdmin::removeHandler(const std::string&) { return true; }

const Network::Socket& ValidationAdmin::socket() { return *socket_; }

ConfigTracker& ValidationAdmin::getConfigTracker() { return config_tracker_; }

void ValidationAdmin::startHttpListener(std::list<AccessLog::InstanceSharedPtr>,
                                        Network::Address::InstanceConstSharedPtr,
                                        Network::Socket::OptionsSharedPtr) {}

Http::Code ValidationAdmin::request(absl::string_view, absl::string_view, Http::ResponseHeaderMap&,
                                    std::string&) {
  PANIC("not implemented");
}

void ValidationAdmin::addListenerToHandler(Network::ConnectionHandler*) {}

} // namespace Server
} // namespace Envoy
