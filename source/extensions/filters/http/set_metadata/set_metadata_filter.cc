#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"

#include "source/common/config/well_known_names.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

Config::Config(const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config) {
  if (proto_config.has_value() && !proto_config.metadata_namespace().empty()) {
    UntypedMetadataEntry deprecated_api_val{true, proto_config.metadata_namespace(),
                                            proto_config.value()};
    untyped_.emplace_back(deprecated_api_val);
  }

  for (const auto& metadata : proto_config.metadata()) {
    if (metadata.has_untyped_value()) {
      UntypedMetadataEntry untyped_entry{metadata.allow_overwrite(), metadata.metadata_namespace(),
                                         metadata.untyped_value()};
      untyped_.emplace_back(untyped_entry);
    } else {
      TypedMetadataEntry typed_entry{metadata.allow_overwrite(), metadata.metadata_namespace(),
                                     metadata.typed_value()};
      typed_.emplace_back(typed_entry);
    }
  }
}

SetMetadataFilter::SetMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

SetMetadataFilter::~SetMetadataFilter() = default;

Http::FilterHeadersStatus SetMetadataFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {

  // add configured untyped metadata
  if (config_->untyped().size() > 0) {
    auto& mut_untyped_metadata =
        *decoder_callbacks_->streamInfo().dynamicMetadata().mutable_filter_metadata();

    for (const auto& entry : config_->untyped()) {
      if (!mut_untyped_metadata.contains(entry.metadata_namespace)) {
        mut_untyped_metadata[entry.metadata_namespace] = entry.value;
      } else if (entry.allow_overwrite) {
        // get the existing metadata at this key for merging
        ProtobufWkt::Struct& orig_fields = mut_untyped_metadata[entry.metadata_namespace];
        const auto& to_merge = entry.value;

        // merge the new metadata into the existing metadata
        StructUtil::update(orig_fields, to_merge);
      }
    }
  }

  // add configured typed metadata
  if (config_->typed().size() > 0) {
    auto& mut_typed_metadata =
        *decoder_callbacks_->streamInfo().dynamicMetadata().mutable_typed_filter_metadata();

    for (const auto& entry : config_->typed()) {
      if (!mut_typed_metadata.contains(entry.metadata_namespace) || entry.allow_overwrite) {
        // overwrite the existing typed metadata at this key
        mut_typed_metadata[entry.metadata_namespace] = entry.value;
      }
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus SetMetadataFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

void SetMetadataFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
