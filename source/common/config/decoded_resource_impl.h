#pragma once

#include "envoy/common/optref.h"
#include "envoy/config/subscription.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/protobuf/utility.h"

#include "xds/core/v3/collection_entry.pb.h"

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

class DecodedResourceImpl;
using DecodedResourceImplPtr = std::unique_ptr<DecodedResourceImpl>;

class DecodedResourceImpl : public DecodedResource {
public:
  static DecodedResourceImplPtr fromResource(OpaqueResourceDecoder& resource_decoder,
                                             const ProtobufWkt::Any& resource,
                                             const std::string& version) {
    if (resource.Is<envoy::service::discovery::v3::Resource>()) {
      envoy::service::discovery::v3::Resource r;
      MessageUtil::unpackToOrThrow(resource, r);

      r.set_version(version);

      return std::make_unique<DecodedResourceImpl>(resource_decoder, r);
    }

    return std::unique_ptr<DecodedResourceImpl>(new DecodedResourceImpl(
        resource_decoder, absl::nullopt, Protobuf::RepeatedPtrField<std::string>(), resource, true,
        version, absl::nullopt, absl::nullopt));
  }

  static DecodedResourceImplPtr
  fromResource(OpaqueResourceDecoder& resource_decoder,
               const envoy::service::discovery::v3::Resource& resource) {
    return std::make_unique<DecodedResourceImpl>(resource_decoder, resource);
  }

  DecodedResourceImpl(OpaqueResourceDecoder& resource_decoder,
                      const envoy::service::discovery::v3::Resource& resource)
      : DecodedResourceImpl(
            resource_decoder, resource.name(), resource.aliases(), resource.resource(),
            resource.has_resource(), resource.version(),
            resource.has_ttl() ? absl::make_optional(std::chrono::milliseconds(
                                     DurationUtil::durationToMilliseconds(resource.ttl())))
                               : absl::nullopt,
            resource.has_metadata() ? absl::make_optional(resource.metadata()) : absl::nullopt) {}
  DecodedResourceImpl(OpaqueResourceDecoder& resource_decoder,
                      const xds::core::v3::CollectionEntry::InlineEntry& inline_entry)
      : DecodedResourceImpl(resource_decoder, inline_entry.name(),
                            Protobuf::RepeatedPtrField<std::string>(), inline_entry.resource(),
                            true, inline_entry.version(), absl::nullopt, absl::nullopt) {}
  DecodedResourceImpl(ProtobufTypes::MessagePtr resource, const std::string& name,
                      const std::vector<std::string>& aliases, const std::string& version)
      : resource_(std::move(resource)), has_resource_(true), name_(name), aliases_(aliases),
        version_(version), ttl_(absl::nullopt), metadata_(absl::nullopt) {}

  // Config::DecodedResource
  const std::string& name() const override { return name_; }
  const std::vector<std::string>& aliases() const override { return aliases_; }
  const std::string& version() const override { return version_; };
  const Protobuf::Message& resource() const override { return *resource_; };
  bool hasResource() const override { return has_resource_; }
  absl::optional<std::chrono::milliseconds> ttl() const override { return ttl_; }
  const OptRef<const envoy::config::core::v3::Metadata> metadata() const override {
    return metadata_.has_value() ? makeOptRef(metadata_.value()) : absl::nullopt;
  }

private:
  DecodedResourceImpl(OpaqueResourceDecoder& resource_decoder, absl::optional<std::string> name,
                      const Protobuf::RepeatedPtrField<std::string>& aliases,
                      const ProtobufWkt::Any& resource, bool has_resource,
                      const std::string& version, absl::optional<std::chrono::milliseconds> ttl,
                      const absl::optional<envoy::config::core::v3::Metadata>& metadata)
      : resource_(resource_decoder.decodeResource(resource)), has_resource_(has_resource),
        name_(name ? *name : resource_decoder.resourceName(*resource_)),
        aliases_(repeatedPtrFieldToVector(aliases)), version_(version), ttl_(ttl),
        metadata_(metadata) {}

  const ProtobufTypes::MessagePtr resource_;
  const bool has_resource_;
  const std::string name_;
  const std::vector<std::string> aliases_;
  const std::string version_;
  // Per resource TTL.
  const absl::optional<std::chrono::milliseconds> ttl_;

  // This is the metadata info under the Resource wrapper.
  // It is intended to be consumed in the xds_config_tracker extension.
  const absl::optional<envoy::config::core::v3::Metadata> metadata_;
};

struct DecodedResourcesWrapper {
  DecodedResourcesWrapper() = default;
  DecodedResourcesWrapper(OpaqueResourceDecoder& resource_decoder,
                          const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                          const std::string& version) {
    for (const auto& resource : resources) {
      pushBack((DecodedResourceImpl::fromResource(resource_decoder, resource, version)));
    }
  }

  void pushBack(Config::DecodedResourcePtr&& resource) {
    owned_resources_.push_back(std::move(resource));
    refvec_.emplace_back(*owned_resources_.back());
  }

  std::vector<Config::DecodedResourcePtr> owned_resources_;
  std::vector<Config::DecodedResourceRef> refvec_;
};

} // namespace Config
} // namespace Envoy
