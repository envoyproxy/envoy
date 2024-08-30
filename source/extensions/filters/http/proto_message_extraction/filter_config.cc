#include "source/extensions/filters/http/proto_message_extraction/filter_config.h"

#include <memory>
#include <utility>

#include "envoy/api/api.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/common.h"
#include "source/extensions/filters/http/proto_message_extraction/extractor.h"

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "grpc_transcoding/type_helper.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageExtraction {
namespace {

using ::envoy::extensions::filters::http::proto_message_extraction::v3::
    ProtoMessageExtractionConfig;
using ::google::grpc::transcoding::TypeHelper;
} // namespace

FilterConfig::FilterConfig(const ProtoMessageExtractionConfig& proto_config,
                           std::unique_ptr<ExtractorFactory> extractor_factory, Api::Api& api)
    : proto_config_(proto_config) {
  initDescriptorPool(api);

  type_helper_ =
      std::make_unique<const TypeHelper>(Envoy::Protobuf::util::NewTypeResolverForDescriptorPool(
          Envoy::Grpc::Common::typeUrlPrefix(), descriptor_pool_.get()));

  type_finder_ = std::make_unique<const TypeFinder>(
      [this](absl::string_view type_url) -> const ::Envoy::ProtobufWkt::Type* {
        return type_helper_->Info()->GetTypeByTypeUrl(type_url);
      });

  initExtractors(*extractor_factory);
}

const Extractor* FilterConfig::findExtractor(absl::string_view proto_path) const {
  if (!proto_path_to_extractor_.contains(proto_path)) {
    return nullptr;
  }
  return proto_path_to_extractor_.find(proto_path)->second.get();
}

void FilterConfig::initExtractors(ExtractorFactory& extractor_factory) {
  for (const auto& it : proto_config_.extraction_by_method()) {
    auto* method = descriptor_pool_->FindMethodByName(it.first);

    if (method == nullptr) {
      throw EnvoyException(fmt::format(
          "couldn't find the gRPC method `{}` defined in the proto descriptor", it.first));
    }

    auto extractor = extractor_factory.createExtractor(
        *type_helper_, *type_finder_,
        Envoy::Grpc::Common::typeUrlPrefix() + "/" + method->input_type()->full_name(),
        Envoy::Grpc::Common::typeUrlPrefix() + "/" + method->output_type()->full_name(), it.second);
    if (!extractor.ok()) {
      throw EnvoyException(fmt::format("couldn't init extractor for method `{}`: {}", it.first,
                                       extractor.status().message()));
    }

    ENVOY_LOG_MISC(debug, "registered field extraction for gRPC method `{}`", it.first);
    proto_path_to_extractor_.emplace(it.first, std::move(extractor.value()));
  }
}

void FilterConfig::initDescriptorPool(Api::Api& api) {
  Envoy::Protobuf::FileDescriptorSet descriptor_set;
  const ::envoy::config::core::v3::DataSource& descriptor_config = proto_config_.data_source();

  auto pool = std::make_unique<Envoy::Protobuf::DescriptorPool>();

  switch (descriptor_config.specifier_case()) {
  case envoy::config::core::v3::DataSource::SpecifierCase::kFilename: {
    auto file_or_error = api.fileSystem().fileReadToEnd(descriptor_config.filename());
    if (!file_or_error.status().ok() || !descriptor_set.ParseFromString(file_or_error.value())) {
      throw Envoy::EnvoyException(fmt::format("unable to parse proto descriptor from file `{}`",
                                              descriptor_config.filename()));
    }
    break;
  }
  case envoy::config::core::v3::DataSource::SpecifierCase::kInlineBytes: {
    if (!descriptor_set.ParseFromString(descriptor_config.inline_bytes())) {
      throw Envoy::EnvoyException(
          fmt::format("unable to parse proto descriptor from inline bytes: {}",
                      descriptor_config.inline_bytes()));
    }
    break;
  }
  default: {
    throw Envoy::EnvoyException(
        fmt::format("unsupported DataSource case `{}` for configuring `descriptor_set`",
                    static_cast<int>(descriptor_config.specifier_case())));
  }
  }

  for (const auto& file : descriptor_set.file()) {
    pool->BuildFile(file);
  }
  descriptor_pool_ = std::move(pool);
}

} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
