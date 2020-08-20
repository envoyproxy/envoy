#pragma once

#include <string>

#include "envoy/config/listener/v3/listener.pb.h"

#include "common/protobuf/utility.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Server {
namespace {

inline envoy::config::listener::v3::Listener parseListenerFromV3Yaml(const std::string& yaml,
                                                                     bool avoid_boosting = true) {
  envoy::config::listener::v3::Listener listener;
  TestUtility::loadFromYaml(yaml, listener, true, avoid_boosting);
  return listener;
}

} // namespace
} // namespace Server
} // namespace Envoy
