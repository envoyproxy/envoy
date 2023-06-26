#include "source/extensions/filters/http/grpc_field_extraction/filter_config.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {
namespace {

// Turns a '/package.Service/method' to 'package.Service.method' which is
// the form suitable for the proto db lookup.
std::string GrpcMethodNameToFullMethodName(const std::string& method) {
  std::string clean_input = method.substr(1);
  size_t first_slash = method.find('/', 0);
  if (first_slash == std::string::npos) {
    ENVOY_LOG_MISC(info, "method does not contain second slash: {}", method);
    return method;
  }
  size_t extra_slash = method.find('/', first_slash);
  if (extra_slash != std::string::npos) {
    ENVOY_LOG_MISC(info,
                   "Method contains too many slashes:{}. Will convert all '/' to '.'.",
                   method);
  }

  std::replace(clean_input.begin(), clean_input.end(), '/', '.');
  return clean_input;
}

}
FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig&
    proto_config,
    ExtractorFactory& extractor_factory)
    : proto_config_(proto_config), extractor_factory_(extractor_factory) {
  Envoy::Protobuf::FileDescriptorSet descriptor_set;
  if (!descriptor_set.ParseFromString(proto_config_.proto_descriptor_bin())) {
    throw Envoy::EnvoyException("Unable to parse proto descriptor");
  }

  for (const auto& file: descriptor_set.file()) {
    if (descriptor_pool_.BuildFile(file) == nullptr) {
      throw Envoy::EnvoyException("Unable to build proto descriptor pool");
    }
  }

  for (const auto& it: proto_config_.extractions_by_method()) {
    auto proto_method_name = GrpcMethodNameToFullMethodName(it.first);
    auto* method = descriptor_pool_.FindMethodByName(proto_method_name);
    if (method == nullptr) {
      throw Envoy::EnvoyException(fmt::format(
          "couldn't find the gRPC method `{}` defined in proto descriptor",
          it.first));
    }

    configured_grpc_methods_.emplace(it.first, proto_method_name);
  }

  type_helper_ = std::make_unique<google::grpc::transcoding::TypeHelper>(
      google::protobuf::util::NewTypeResolverForDescriptorPool(
          Envoy::Grpc::Common::typeUrlPrefix(), &descriptor_pool_));
}

  TypeFinder FilterConfig::createTypeFinder()  {
    return [this](absl::string_view type_url) -> const google::protobuf::Type* {
      return type_helper_->Info()->GetTypeByTypeUrl(type_url);
    };
  }

absl::StatusOr<PerMethodExtraction>
FilterConfig::FindPerMethodExtraction(absl::string_view path) {
  auto it = configured_grpc_methods_.find(path);
  if (it == configured_grpc_methods_.end()) {
    return absl::UnavailableError(fmt::format(
        "gRPC method `{}` isn't configured for field extraction",
        path));
  }

  return PerMethodExtraction{
       Envoy::Grpc::Common::typeUrlPrefix() + "/" +
           descriptor_pool_.FindMethodByName(it->second)->input_type()->full_name(),
      &proto_config_.extractions_by_method().at(path),
  };
}
}