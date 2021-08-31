#pragma once

#include <string>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener.pb.validate.h"

#include "source/common/protobuf/utility.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Server {
namespace {

inline envoy::config::listener::v3::Listener parseListenerFromV3Yaml(const std::string& yaml) {
  envoy::config::listener::v3::Listener listener;
  TestUtility::loadFromYamlAndValidate(yaml, listener);
  return listener;
}

} // namespace
} // namespace Server
} // namespace Envoy
