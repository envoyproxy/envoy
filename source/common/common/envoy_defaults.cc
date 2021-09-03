#include "source/common/common/envoy_defaults.h"

#include "source/common/common/manifest.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
DefaultsProfile::DefaultsProfile(Profile profile) {
  if (profile == Safe) {
    MessageUtil::loadFromYaml(manifest_yaml, defaults_manifest_,
                              ProtobufMessage::getNullValidationVisitor());
  }
}

const DefaultsProfile& DefaultsProfile::get() {
  if (DefaultsProfileSingleton::getExisting()) {
    return DefaultsProfileSingleton::get();
  }

  CONSTRUCT_ON_FIRST_USE(DefaultsProfile);
};

// Retrieve number value in defaults profile @ config_name.field if it exists, otherwise return
// `default_value`
double DefaultsProfile::getNumber(const ConfigContext& config, const std::string& field,
                                  double default_value) const {
  absl::optional<ProtobufWkt::Value> value = getProtoValue(std::string(config.getContext()), field);

  if (value && value->has_number_value()) {
    return value->number_value();
  }

  return default_value;
}

// Retrieve ms value in defaults profile @ config_name.field if it exists, otherwise return
// `default_value`
std::chrono::milliseconds DefaultsProfile::getMs(const ConfigContext& config,
                                                 const std::string& field,
                                                 int default_value) const {
  absl::optional<ProtobufWkt::Value> value = getProtoValue(std::string(config.getContext()), field);

  if (value && value->has_string_value()) {
    ProtobufWkt::Duration duration;
    auto result = ProtobufUtil::JsonStringToMessage(absl::StrCat("\"", value->string_value(), "\""),
                                                    &duration);
    return std::chrono::milliseconds(result.ok() ? DurationUtil::durationToMilliseconds(duration)
                                                 : default_value);
  }

  return std::chrono::milliseconds(default_value);
}

// Retrieve bool value in defaults profile @ config_name.field if it exists, otherwise return
// `default_value`
bool DefaultsProfile::getBool(const ConfigContext& config, const std::string& field,
                              bool default_value) const {
  absl::optional<ProtobufWkt::Value> value = getProtoValue(std::string(config.getContext()), field);

  if (value && value->has_bool_value()) {
    return value->bool_value();
  }

  return default_value;
}

// Search defaults profile for `field`, return value if found
absl::optional<ProtobufWkt::Value> DefaultsProfile::getProtoValue(const std::string config_name,
                                                                  const std::string& field) const {
  const auto& fields_map = defaults_manifest_.fields();
  auto config_it = fields_map.find(config_name);

  // case: `field` exists in a map of multiple fields
  if (config_it != fields_map.end()) {
    ASSERT(config_it->second.has_edge_config() && config_it->second.edge_config().has_example());

    const ProtobufWkt::Value& submap = (config_it->second).edge_config().example();
    if (!submap.has_struct_value()) {
      return absl::nullopt;
    }

    const Protobuf::Map<std::string, ProtobufWkt::Value>& fields_submap =
        submap.struct_value().fields();
    auto value_it = fields_submap.find(field);
    return value_it == fields_submap.end() ? absl::nullopt : absl::make_optional(value_it->second);
  }

  // otherwise, `field` may exist in a single key-value pairing
  std::string full_field_name = absl::StrCat(config_name, ".", field);
  auto value_it = fields_map.find(full_field_name);
  if (value_it == fields_map.end()) {
    return absl::nullopt;
  }
  ASSERT(value_it->second.has_edge_config() && value_it->second.edge_config().has_example());
  return absl::make_optional(value_it->second.edge_config().example());
}

} // namespace Envoy
