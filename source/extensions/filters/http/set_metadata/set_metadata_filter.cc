#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

SetMetadataFilter::SetMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

SetMetadataFilter::~SetMetadataFilter() = default;

Http::FilterHeadersStatus SetMetadataFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {

  // Add configured untyped metadata.
  if (!config_->untyped().empty()) {
    auto& mut_untyped_metadata =
        *decoder_callbacks_->streamInfo().dynamicMetadata().mutable_filter_metadata();

    for (const auto& entry : config_->untyped()) {
      if (!mut_untyped_metadata.contains(entry.metadata_namespace)) {
        // Insert the new entry.
        mut_untyped_metadata[entry.metadata_namespace] = entry.value;
      } else if (entry.allow_overwrite) {
        // Get the existing metadata at this key for merging.
        Protobuf::Struct& orig_fields = mut_untyped_metadata[entry.metadata_namespace];
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
        *decoder_callbacks_->streamInfo().dynamicMetadata().mutable_typed_filter_metadata();

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
