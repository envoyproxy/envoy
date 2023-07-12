#include "source/extensions/filters/http/grpc_field_extraction/filter_config.h"
#include "source/common/common/fmt.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig&
        proto_config,
    ExtractorFactory& extractor_factory, Api::Api& api)
    : proto_config_(proto_config), extractor_factory_(extractor_factory) {
  Envoy::Protobuf::FileDescriptorSet descriptor_set;

  auto& descriptor_config = proto_config.descriptor_set();

  switch (descriptor_config.specifier_case()) {
  case envoy::config::core::v3::DataSource::SpecifierCase::kFilename: {
    if (!descriptor_set.ParseFromString(api.fileSystem().fileReadToEnd(descriptor_config.filename()))) {
      throw Envoy::EnvoyException("Unable to parse proto descriptor");
    }
    break;
  }
  case envoy::config::core::v3::DataSource::SpecifierCase::kInlineBytes: {
    if (!descriptor_set.ParseFromString(descriptor_config.inline_bytes())) {
      throw Envoy::EnvoyException("Unable to parse proto descriptor");
    }
    break;
  }
  default: {
    throw Envoy::EnvoyException(fmt::format("Unsupported DataSource case `{}` for configuring `descriptor_set`", descriptor_config.specifier_case()));
    break;
  }
  }

  for (const auto& file : descriptor_set.file()) {
    if (descriptor_pool_.BuildFile(file) == nullptr) {
      throw Envoy::EnvoyException("Unable to build proto descriptor pool");
    }
  }
  for (const auto& it : proto_config_.extractions_by_method()) {
    auto* method = descriptor_pool_.FindMethodByName(it.first);
    if (method == nullptr) {
      throw Envoy::EnvoyException(
          fmt::format("couldn't find the gRPC method `{}` defined in the proto descriptor", it.first));
    }
  }

  type_helper_ = std::make_unique<google::grpc::transcoding::TypeHelper>(
      google::protobuf::util::NewTypeResolverForDescriptorPool(Envoy::Grpc::Common::typeUrlPrefix(),
                                                               &descriptor_pool_));
}

TypeFinder FilterConfig::createTypeFinder() {
  return [this](absl::string_view type_url) -> const google::protobuf::Type* {
    return type_helper_->Info()->GetTypeByTypeUrl(type_url);
  };
}

absl::StatusOr<PerMethodExtraction> FilterConfig::FindPerMethodExtraction(absl::string_view proto_path) {
  const auto* md = descriptor_pool_.FindMethodByName(proto_path);
  if (md == nullptr) {
     return absl::UnavailableError(
        fmt::format("gRPC method with protobuf path `{}` isn't configured for field extraction", proto_path));
  }

  return PerMethodExtraction{
      Envoy::Grpc::Common::typeUrlPrefix() + "/" +
          md->input_type()->full_name(),
      &proto_config_.extractions_by_method().at(proto_path),
  };
}
} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction