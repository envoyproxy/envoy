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

  static DefaultsProfile no_singleton_profile_;
  return no_singleton_profile_;
};

// Retrieve number value in defaults profile @ config_name.field if it exists, otherwise return
// `default_value`
double DefaultsProfile::getNumber(const ConfigContext& config, const std::string& field,
                                  double default_value) const {
  absl::optional<ProtobufWkt::Value> value = getProtoValue(std::string(config.getContext()), field);

  if (value && value->has_number_value()) {
    printf("NUMBER VALUE: %f\n\n", value->number_value());
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
    auto result = ProtobufUtil::JsonStringToMessage("\"" + value->string_value() + "\"", &duration);
    // should I use TestUtility::loadFromJson("\"" + value->string_value() + "\"", &duration) ?
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
    // printf("BOOL VALUE: %s\n\n", value->bool_value() ? "true" : "false");
    return value->bool_value();
  }

  return default_value;
}

// Search defaults profile for `field`, return value if found
absl::optional<ProtobufWkt::Value> DefaultsProfile::getProtoValue(const std::string config_name,
                                                                  const std::string& field) const {
  auto fields_map = defaults_manifest_.fields();
  auto config_it = fields_map.find(config_name);

  // case: `field` exists in a map of multiple fields
  if (config_it != fields_map.end()) {
    const ProtobufWkt::Value submap = (config_it->second).edge_config().example();
    if (!submap.has_struct_value())
      return absl::nullopt;
    const Protobuf::Map<std::string, ProtobufWkt::Value> fields_submap =
        submap.struct_value().fields();

    return fields_submap.count(field) ? absl::make_optional(fields_submap.at(field))
                                      : absl::nullopt;
  }

  // otherwise, `field` may exist in a single key-value pairing
  std::string full_field_name = config_name + "." + field;
  return fields_map.count(full_field_name)
             ? absl::make_optional(fields_map.at(full_field_name).edge_config().example())
             : absl::nullopt;
}

} // namespace Envoy
