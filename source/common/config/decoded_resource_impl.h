#pragma once

#include "envoy/config/subscription.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

namespace {

std::vector<std::string>
repeatedPtrFieldToVector(const Protobuf::RepeatedPtrField<std::string>& xs) {
  std::vector<std::string> ys;
  std::copy(xs.begin(), xs.end(), std::back_inserter(ys));
  return ys;
}

} // namespace

class DecodedResourceImpl : public DecodedResource {
public:
  DecodedResourceImpl(OpaqueResourceDecoder& resource_decoder, const ProtobufWkt::Any& resource,
                      const std::string& version)
      : DecodedResourceImpl(resource_decoder, {}, Protobuf::RepeatedPtrField<std::string>(),
                            resource, true, version) {}
  DecodedResourceImpl(OpaqueResourceDecoder& resource_decoder,
                      const envoy::service::discovery::v3::Resource& resource)
      : DecodedResourceImpl(resource_decoder, resource.name(), resource.aliases(),
                            resource.resource(), resource.has_resource(), resource.version()) {}
  DecodedResourceImpl(ProtobufTypes::MessagePtr resource, const std::string& name,
                      const std::vector<std::string>& aliases, const std::string& version)
      : resource_(std::move(resource)), has_resource_(true), name_(name), aliases_(aliases),
        version_(version) {}

  // Config::DecodedResource
  const std::string& name() const override { return name_; }
  const std::vector<std::string>& aliases() const override { return aliases_; }
  const std::string& version() const override { return version_; };
  const Protobuf::Message& resource() const override { return *resource_; };
  bool hasResource() const override { return has_resource_; }

private:
  DecodedResourceImpl(OpaqueResourceDecoder& resource_decoder, absl::optional<std::string> name,
                      const Protobuf::RepeatedPtrField<std::string>& aliases,
                      const ProtobufWkt::Any& resource, bool has_resource,
                      const std::string& version)
      : resource_(resource_decoder.decodeResource(resource)), has_resource_(has_resource),
        name_(name ? *name : resource_decoder.resourceName(*resource_)),
        aliases_(repeatedPtrFieldToVector(aliases)), version_(version) {}

  const ProtobufTypes::MessagePtr resource_;
  const bool has_resource_;
  const std::string name_;
  const std::vector<std::string> aliases_;
  const std::string version_;
};

using DecodedResourceImplPtr = std::unique_ptr<DecodedResourceImpl>;

struct DecodedResourcesWrapper {
  DecodedResourcesWrapper() = default;
  DecodedResourcesWrapper(OpaqueResourceDecoder& resource_decoder,
                          const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                          const std::string& version) {
    for (const auto& resource : resources) {
      owned_resources_.emplace_back(new DecodedResourceImpl(resource_decoder, resource, version));
      refvec_.emplace_back(*owned_resources_.back());
    }
  }

  std::vector<Config::DecodedResourcePtr> owned_resources_;
  std::vector<Config::DecodedResourceRef> refvec_;
};

} // namespace Config
} // namespace Envoy
