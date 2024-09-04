#include "source/extensions/filters/http/grpc_field_extraction/filter_config.h"

#include "source/common/common/fmt.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {
using ::envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig;
using ::google::grpc::transcoding::TypeHelper;
} // namespace

FilterConfig::FilterConfig(const GrpcFieldExtractionConfig& proto_config,
                           std::unique_ptr<ExtractorFactory> extractor_factory, Api::Api& api)
    : proto_config_(proto_config) {
  initDescriptorPool(api);

  type_helper_ =
      std::make_unique<const TypeHelper>(Protobuf::util::NewTypeResolverForDescriptorPool(
          Grpc::Common::typeUrlPrefix(), descriptor_pool_.get()));
  type_finder_ = std::make_unique<const TypeFinder>(
      [this](absl::string_view type_url) -> const Protobuf::Type* {
        return type_helper_->Info()->GetTypeByTypeUrl(type_url);
      });

  initExtractors(*extractor_factory);
}

void FilterConfig::initExtractors(ExtractorFactory& extractor_factory) {
  for (const auto& it : proto_config_.extractions_by_method()) {
    auto* method = descriptor_pool_->FindMethodByName(it.first);
    if (method == nullptr) {
      throw EnvoyException(fmt::format(
          "couldn't find the gRPC method `{}` defined in the proto descriptor", it.first));
    }

    auto extractor = extractor_factory.createExtractor(
        *type_finder_,
        Envoy::Grpc::Common::typeUrlPrefix() + "/" + method->input_type()->full_name(), it.second);
    if (!extractor.ok()) {
      throw EnvoyException(fmt::format("couldn't init extractor for method `{}`: {}", it.first,
                                       extractor.status().message()));
    }

    ENVOY_LOG_MISC(debug, "registered field extraction for gRPC method `{}`", it.first);
    proto_path_to_extractor_.emplace(it.first, std::move(extractor.value()));
  }
}

void FilterConfig::initDescriptorPool(Api::Api& api) {
  Protobuf::FileDescriptorSet descriptor_set;
  auto& descriptor_config = proto_config_.descriptor_set();

  auto pool = std::make_unique<Protobuf::DescriptorPool>();

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

const Extractor* FilterConfig::findExtractor(absl::string_view proto_path) const {
  if (!proto_path_to_extractor_.contains(proto_path)) {
    return nullptr;
  }
  return proto_path_to_extractor_.find(proto_path)->second.get();
}

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
