#pragma once

#include "source/common/protobuf/protobuf.h"
#include "source/common/singleton/threadsafe_singleton.h"

#include "tools/protodoc/manifest.pb.h"

// These macros return the value associated with `field_name` in the protobuf `message` if it
// exists. Otherwise, they return the value associated with `field_name` in the defaults profile if
// it exists. Otherwise, `default_value` is returned.

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
  enum Profile {
    Performant,
    Safe,
  };

  class ConfigContext {
  public:
    ConfigContext() = default;
    ConfigContext(const ConfigContext& context) = default;
    ConfigContext(const Protobuf::Message& config) : ctx_(config.GetDescriptor()->full_name()) {}
    // ConfigContext(const ConfigContext& context) : ctx_(context.ctx_) {}
    ConfigContext(const std::string& context) : ctx_(context) {}
    const absl::string_view getContext() const { return ctx_; }
    ConfigContext& appendField(absl::string_view field) {
      absl::StrAppend(&ctx_, ".", field);
      return *this;
    }

  private:
    std::string ctx_;
  };

  /**
   * DefaultsProfile intended to be constructed as a DefaultsProfileSingleton, so that default
   * values can be accessed globally.
   * @param profile enum value specifying which profile to load
   */
  DefaultsProfile(Profile profile = Performant);
  /**
   * Returns a reference to DefaultsProfile inside singleton if it exists, otherwise returns
   * blank DefaultsProfile.
   */
  static const DefaultsProfile& get();

  /**
   * Getters return the value associated with `field` in the DefaultsProfile if it exists,
   * otherwise they return `default_value`.
   * @param config encapsulates the full name of the Protobuf config message under which
   * `field` should appear
   * @param field to be searched for in the defaults profile
   * @param default_value to be returned if `field` not present in the defaults profile
   */
  double getNumber(const ConfigContext& config, const std::string& field,
                   double default_value) const;
  std::chrono::milliseconds getMs(const ConfigContext& config, const std::string& field,
                                  int default_value) const;
  bool getBool(const ConfigContext& config, const std::string& field, bool default_value) const;

private:
  /**
   * Searches the defaults profile for `field`, which may exist in a map of multiple
   * fields keyed by `config_name`, or as a single key that is `config_name` concatenated
   * with '.' + `field`. If `field` is found, the Proto value associated with `field`
   * is returned. Otherwise, absl::nullopt returns.
   * @param config_name full name of the Protobuf config message under which `field`
   * should appear. E.g. "envoy.config.cluster.v3.Cluster"
   * @param field the default value associated with this field is returned if present
   * in the defaults profile. E.g. "per_connection_buffer_limit_bytes"
   */
  absl::optional<ProtobufWkt::Value> getProtoValue(const std::string config_name,
                                                   const std::string& field) const;
  tools::protodoc::Manifest defaults_manifest_;
};

using DefaultsProfileSingleton = InjectableSingleton<DefaultsProfile>;
using ScopedDefaultsProfileSingleton = ScopedInjectableLoader<DefaultsProfile>;

} // namespace Envoy
