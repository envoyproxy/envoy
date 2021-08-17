#pragma once

#include "source/common/protobuf/protobuf.h"
#include "source/common/singleton/threadsafe_singleton.h"

#include "tools/protodoc/manifest.pb.h"

#define PROTOBUF_GET_NUMBER_OR_PROFILE_DEFAULT(message, config, field_name, default_value)         \
  ([&](const auto& msg) -> double {                                                                \
    if (msg.has_##field_name()) {                                                                  \
      return msg.field_name().value();                                                             \
    }                                                                                              \
    return DefaultsProfile::get().getNumber(config, #field_name, default_value);                   \
  }((message)))

#define PROTOBUF_GET_MS_OR_PROFILE_DEFAULT(message, config, field_name, default_value)             \
  ([&](const auto& msg) -> std::chrono::milliseconds {                                             \
    if (msg.has_##field_name()) {                                                                  \
      return std::chrono::milliseconds(DurationUtil::durationToMilliseconds(msg.field_name()));    \
    }                                                                                              \
    return DefaultsProfile::get().getMs(config, #field_name, default_value);                       \
  }((message)))

#define PROTOBUF_GET_BOOL_OR_PROFILE_DEFAULT(message, config, field_name, default_value)           \
  ([&](const auto& msg) -> bool {                                                                  \
    if (msg.has_##field_name()) {                                                                  \
      return msg.field_name().value();                                                             \
    }                                                                                              \
    return DefaultsProfile::get().getBool(config, #field_name, default_value);                     \
  }((message)))

namespace Envoy {

struct DefaultsProfile {
  class ConfigContext {
  public:
    ConfigContext() = default;
    ConfigContext(const Protobuf::Message& config) : ctx_(config.GetDescriptor()->full_name()) {}
    ConfigContext(const ConfigContext& context) : ctx_(context.ctx_) {}
    const absl::string_view getContext() const { return ctx_; }
    ConfigContext& appendField(absl::string_view field) {
      absl::StrAppend(&ctx_, ".", field);
      return *this;
    }

  private:
    std::string ctx_;
  };

  DefaultsProfile();
  static const DefaultsProfile& get();

  double getNumber(const ConfigContext& config, const std::string& field,
                   double default_value) const;
  std::chrono::milliseconds getMs(const ConfigContext& config, const std::string& field,
                                  int default_value) const;
  bool getBool(const ConfigContext& config, const std::string& field, bool default_value) const;

private:
  absl::optional<ProtobufWkt::Value> getProtoValue(const std::string config_name,
                                                   const std::string& field) const;
  tools::protodoc::Manifest defaults_manifest_;
};

using DefaultsProfileSingleton = InjectableSingleton<DefaultsProfile>;
using ScopedDefaultsProfileSingleton = ScopedInjectableLoader<DefaultsProfile>;

} // namespace Envoy
