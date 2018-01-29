#pragma once

#include "envoy/json/json_object.h"

#include "api/lds.pb.h"

namespace Envoy {
namespace Config {

class LdsJson {
public:
  /**
   * Translate a v1 JSON Listener to v2 envoy::api::v2::Listener.
   * @param json_listener source v1 JSON Listener object.
   * @param listener destination v2 envoy::api::v2::Listener.
   */
  static void translateListener(const Json::Object& json_listener,
                                envoy::api::v2::Listener& listener);
};

} // namespace Config
} // namespace Envoy
