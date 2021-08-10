#pragma once

#include "source/common/singleton/threadsafe_singleton.h"

#include "tools/protodoc/manifest.pb.h"

#define PROTOBUF_GET_NUMBER_OR_PROFILE_DEFAULT(config, message, field_name, default_value)         \
  ([&](const auto& msg) -> double {                                                                \
    if (msg.has_##field_name()) {                                                                  \
      printf("\n\n %s.%s = %d\n\n", config.GetDescriptor()->full_name().c_str(), #field_name,      \
             msg.field_name().value());                                                            \
      return msg.field_name().value();                                                             \
    }                                                                                              \
    return DefaultsProfile::get().get_number(config.GetDescriptor()->full_name(), #field_name,     \
                                             default_value);                                       \
  }((message)))

#define PROTOBUF_GET_MS_OR_PROFILE_DEFAULT(config, message, field_name, default_value)             \
  ([&](const auto& msg) -> std::chrono::milliseconds {                                             \
    if (msg.has_##field_name()) {                                                                  \
      printf("\n\n %s.%s = %lu\n\n", config.GetDescriptor()->full_name().c_str(), #field_name,     \
             DurationUtil::durationToMilliseconds(msg.field_name()));                              \
      return std::chrono::milliseconds(DurationUtil::durationToMilliseconds(msg.field_name()));    \
    }                                                                                              \
    return DefaultsProfile::get().get_ms(config.GetDescriptor()->full_name(), #field_name,         \
                                         default_value);                                           \
  }((message)))

#define PROTOBUF_GET_BOOL_OR_PROFILE_DEFAULT(config, message, field_name, default_value)           \
  ([&](const auto& msg) -> bool {                                                                  \
    if (msg.has_##field_name()) {                                                                  \
      printf("\n\n %s.%s = %s\n\n", config.GetDescriptor()->full_name().c_str(), #field_name,      \
             msg.field_name().value() ? "true" : "false");                                         \
      return msg.field_name().value();                                                             \
    }                                                                                              \
    return DefaultsProfile::get().get_bool(config.GetDescriptor()->full_name(), #field_name,       \
                                           default_value);                                         \
  }((message)))

namespace Envoy {

struct DefaultsProfile {
  DefaultsProfile();
  static const DefaultsProfile& get();

  double get_number(const std::string config_name, const std::string& field,
                    double default_value) const;
  std::chrono::milliseconds get_ms(const std::string config_name, const std::string& field,
                                   int default_value) const;
  bool get_bool(const std::string config_name, const std::string& field, bool default_value) const;

private:
  absl::optional<ProtobufWkt::Value> get_proto_value(const std::string config_name,
                                                     const std::string& field) const;
  tools::protodoc::Manifest defaults_manifest_;
};

using DefaultsProfileSingleton = InjectableSingleton<DefaultsProfile>;
using ScopedDefaultsProfileSingleton = ScopedInjectableLoader<DefaultsProfile>;

} // namespace Envoy
