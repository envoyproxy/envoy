#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"

#include "source/common/common/base64.h"
#include "source/common/config/well_known_names.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/server/generic_factory_context.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

absl::StatusOr<std::shared_ptr<Config>>
Config::create(const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config,
               Stats::Scope& scope, const std::string& stats_prefix,
               Server::Configuration::FactoryContext& factory_context) {
  // Create a GenericFactoryContext from FactoryContext.
  Server::GenericFactoryContextImpl generic_context(factory_context.serverFactoryContext(),
                                                    factory_context.messageValidationVisitor());
  return create(proto_config, scope, stats_prefix, generic_context);
}

absl::StatusOr<std::shared_ptr<Config>>
Config::create(const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config,
               Stats::Scope& scope, const std::string& stats_prefix,
               Server::Configuration::GenericFactoryContext& factory_context) {
  absl::Status creation_status = absl::OkStatus();
  auto cfg = std::shared_ptr<Config>(
      new Config(proto_config, scope, stats_prefix, factory_context, creation_status));
  RETURN_IF_NOT_OK_REF(creation_status);
  return cfg;
}

Config::Config(const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config,
               Stats::Scope& scope, const std::string& stats_prefix,
               Server::Configuration::GenericFactoryContext& factory_context,
               absl::Status& creation_status)
    : stats_(generateStats(stats_prefix, scope)) {
  parseConfig(proto_config, factory_context, creation_status);
}

void Config::parseConfig(
    const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context, absl::Status& creation_status) {
  if (proto_config.has_value() && !proto_config.metadata_namespace().empty()) {
    UntypedMetadataEntry deprecated_api_val{true, proto_config.metadata_namespace(),
                                            proto_config.value()};
    untyped_.emplace_back(deprecated_api_val);
  }

  // Parse apply_on configuration.
  apply_on_ = proto_config.apply_on();

  for (const auto& metadata : proto_config.metadata()) {
    if (metadata.has_value()) {
      UntypedMetadataEntry untyped_entry{metadata.allow_overwrite(), metadata.metadata_namespace(),
                                         metadata.value()};
      untyped_.emplace_back(untyped_entry);
    } else if (metadata.has_typed_value()) {
      TypedMetadataEntry typed_entry{metadata.allow_overwrite(), metadata.metadata_namespace(),
                                     metadata.typed_value()};
      typed_.emplace_back(typed_entry);
    } else if (metadata.has_format_string()) {
      auto formatter_or_error = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
          metadata.format_string(), factory_context);
      if (!formatter_or_error.ok()) {
        creation_status = absl::InvalidArgumentError(
            fmt::format("Failed to create formatter for metadata namespace '{}': {}",
                        metadata.metadata_namespace(), formatter_or_error.status().message()));
        return;
      }
      FormattedMetadataEntry formatted_entry{
          metadata.allow_overwrite(), metadata.metadata_namespace(),
          std::move(formatter_or_error.value()), metadata.type(), metadata.encode()};
      formatted_.emplace_back(std::move(formatted_entry));
    } else {
      ENVOY_LOG(warn, "set_metadata filter configuration contains metadata entries without value, "
                      "typed_value, or format_string.");
    }
  }
}

FilterStats Config::generateStats(const std::string& prefix, Stats::Scope& scope) {
  std::string final_prefix = prefix + "set_metadata.";
  return {ALL_SET_METADATA_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

SetMetadataFilter::SetMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

SetMetadataFilter::~SetMetadataFilter() = default;

Http::FilterHeadersStatus SetMetadataFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  if (config_->applyOn() == envoy::extensions::filters::http::set_metadata::v3::Config::REQUEST ||
      config_->applyOn() == envoy::extensions::filters::http::set_metadata::v3::Config::BOTH) {
    Formatter::HttpFormatterContext context(&headers);
    processMetadata(*decoder_callbacks_, context);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus SetMetadataFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (config_->applyOn() == envoy::extensions::filters::http::set_metadata::v3::Config::RESPONSE ||
      config_->applyOn() == envoy::extensions::filters::http::set_metadata::v3::Config::BOTH) {
    Formatter::HttpFormatterContext context(nullptr, &headers);
    processMetadata(*encoder_callbacks_, context);
  }
  return Http::FilterHeadersStatus::Continue;
}

void SetMetadataFilter::processMetadata(Http::StreamFilterCallbacks& callbacks,
                                        Formatter::HttpFormatterContext& context) {
  // Add configured untyped metadata.
  if (!config_->untyped().empty()) {
    auto& mut_untyped_metadata =
        *callbacks.streamInfo().dynamicMetadata().mutable_filter_metadata();

    for (const auto& entry : config_->untyped()) {
      if (!mut_untyped_metadata.contains(entry.metadata_namespace)) {
        // Insert the new entry.
        mut_untyped_metadata[entry.metadata_namespace] = entry.value;
      } else if (entry.allow_overwrite) {
        // Get the existing metadata at this key for merging.
        ProtobufWkt::Struct& orig_fields = mut_untyped_metadata[entry.metadata_namespace];
        const auto& to_merge = entry.value;

        // Merge the new metadata into the existing metadata.
        StructUtil::update(orig_fields, to_merge);
      } else {
        // The entry exists, and we are not allowed to overwrite -- emit a stat.
        config_->stats().overwrite_denied_.inc();
      }
    }
  }

  // Add configured typed metadata.
  if (!config_->typed().empty()) {
    auto& mut_typed_metadata =
        *callbacks.streamInfo().dynamicMetadata().mutable_typed_filter_metadata();

    for (const auto& entry : config_->typed()) {
      if (!mut_typed_metadata.contains(entry.metadata_namespace)) {
        // Insert the new entry.
        mut_typed_metadata[entry.metadata_namespace] = entry.value;
      } else if (entry.allow_overwrite) {
        // Overwrite the existing typed metadata at this key.
        mut_typed_metadata[entry.metadata_namespace] = entry.value;
      } else {
        // The entry exists, and we are not allowed to overwrite -- emit a stat.
        config_->stats().overwrite_denied_.inc();
      }
    }
  }

  // Add configured formatted metadata.
  if (!config_->formatted().empty()) {
    auto& mut_untyped_metadata =
        *callbacks.streamInfo().dynamicMetadata().mutable_filter_metadata();

    for (const auto& entry : config_->formatted()) {
      // Format the value using substitution formatter to expand placeholders like
      // %VIRTUAL_CLUSTER_NAME%.
      std::string formatted_value =
          entry.formatter->formatWithContext(context, callbacks.streamInfo());

      // Handle encoding if specified.
      if (entry.encode == envoy::extensions::filters::http::set_metadata::v3::Metadata::BASE64) {
        std::string decoded_value = Base64::decodeWithoutPadding(formatted_value);
        if (decoded_value.empty() && !formatted_value.empty()) {
          ENVOY_LOG(debug, "Base64 decode failed for metadata namespace '{}'",
                    entry.metadata_namespace);
          continue;
        }
        formatted_value = std::move(decoded_value);
      }

      // Create a Struct with the formatted value based on type.
      ProtobufWkt::Struct formatted_struct;
      ProtobufWkt::Value* value_field = &(*formatted_struct.mutable_fields())["value"];

      switch (entry.type) {
      case envoy::extensions::filters::http::set_metadata::v3::Metadata::STRING:
        value_field->set_string_value(formatted_value);
        break;
      case envoy::extensions::filters::http::set_metadata::v3::Metadata::NUMBER: {
        double dval;
        if (absl::SimpleAtod(StringUtil::trim(formatted_value), &dval)) {
          value_field->set_number_value(dval);
        } else {
          ENVOY_LOG(debug, "Failed to convert '{}' to number for metadata namespace '{}'",
                    formatted_value, entry.metadata_namespace);
          continue;
        }
        break;
      }
      case envoy::extensions::filters::http::set_metadata::v3::Metadata::PROTOBUF_VALUE: {
        if (!value_field->ParseFromString(formatted_value)) {
          ENVOY_LOG(debug, "Failed to parse protobuf value for metadata namespace '{}'",
                    entry.metadata_namespace);
          continue;
        }
        break;
      }
      default:
        ENVOY_LOG(warn, "Unknown ValueType for metadata namespace '{}'", entry.metadata_namespace);
        continue;
      }

      if (!mut_untyped_metadata.contains(entry.metadata_namespace)) {
        // Insert the new entry.
        mut_untyped_metadata[entry.metadata_namespace] = formatted_struct;
      } else if (entry.allow_overwrite) {
        // Get the existing metadata at this key for merging.
        ProtobufWkt::Struct& orig_fields = mut_untyped_metadata[entry.metadata_namespace];

        // Merge the new formatted metadata into the existing metadata.
        StructUtil::update(orig_fields, formatted_struct);
      } else {
        // The entry exists, and we are not allowed to overwrite, we emit a stat.
        config_->stats().overwrite_denied_.inc();
      }
    }
  }
}

Http::FilterDataStatus SetMetadataFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

void SetMetadataFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void SetMetadataFilter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
