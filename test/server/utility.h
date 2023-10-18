#pragma once

#include <string>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener.pb.validate.h"

#include "source/common/protobuf/utility.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Server {

inline envoy::config::listener::v3::Listener parseListenerFromV3Yaml(const std::string& yaml) {
  envoy::config::listener::v3::Listener listener;
  TestUtility::loadFromYamlAndValidate(yaml, listener);
  return listener;
}

inline std::string testDomainSocketName() {
#ifdef WIN32
  auto pid = GetCurrentProcessId();
#else
  auto pid = getpid();
#endif
  // It seems like we should use a test tmpdir path for this socket, but unix
  // domain socket length limits prohibit that, so instead we append the
  // process ID to a name in the abstract namespace.
  // Using a tmpdir in /tmp could be another option but would introduce
  // cleanup requirements that are avoided by using the abstract namespace.
  return absl::StrCat("@envoy_domain_socket_", pid);
}

} // namespace Server
} // namespace Envoy
