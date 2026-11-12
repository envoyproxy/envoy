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
  static absl::StatusOr<DecodedResourceImplPtr>
  fromResource(OpaqueResourceDecoder& resource_decoder, const Protobuf::Any& resource,
               const std::string& version, Protobuf::Arena& arena) {
    if (resource.Is<envoy::service::discovery::v3::Resource>()) {
      envoy::service::discovery::v3::Resource r;
      RETURN_IF_NOT_OK(MessageUtil::unpackTo(resource, r));
      r.set_version(version);
      return std::make_unique<DecodedResourceImpl>(resource_decoder, r, arena);
    }

    return std::unique_ptr<DecodedResourceImpl>(new DecodedResourceImpl(
        resource_decoder, std::nullopt, Protobuf::RepeatedPtrField<std::string>(), resource, true,
        version, std::nullopt, std::nullopt, arena));
  }

  static DecodedResourceImplPtr
  fromResource(OpaqueResourceDecoder& resource_decoder,
               const envoy::service::discovery::v3::Resource& resource, Protobuf::Arena& arena) {
    return std::make_unique<DecodedResourceImpl>(resource_decoder, resource, arena);
  }

  DecodedResourceImpl(OpaqueResourceDecoder& resource_decoder,
                      const envoy::service::discovery::v3::Resource& resource,
                      Protobuf::Arena& arena)
      : DecodedResourceImpl(
            resource_decoder, resource.name(), resource.aliases(), resource.resource(),
            resource.has_resource(), resource.version(),
            resource.has_ttl() ? std::make_optional(std::chrono::milliseconds(
                                     DurationUtil::durationToMilliseconds(resource.ttl())))
                               : std::nullopt,
            resource.has_metadata() ? std::make_optional(resource.metadata()) : std::nullopt,
            arena) {}
  DecodedResourceImpl(OpaqueResourceDecoder& resource_decoder,
                      const xds::core::v3::CollectionEntry::InlineEntry& inline_entry,
                      Protobuf::Arena& arena)
      : DecodedResourceImpl(resource_decoder, inline_entry.name(),
                            Protobuf::RepeatedPtrField<std::string>(), inline_entry.resource(),
                            true, inline_entry.version(), std::nullopt, std::nullopt, arena) {}
  DecodedResourceImpl(Protobuf::Arena::UniquePtr<Protobuf::Message> resource,
                      const std::string& name, const std::vector<std::string>& aliases,
                      const std::string& version)
      : resource_(std::move(resource)), has_resource_(true), name_(name), aliases_(aliases),
        version_(version), ttl_(std::nullopt), metadata_(std::nullopt) {}

  // Config::DecodedResource
  const std::string& name() const override { return name_; }
  const std::vector<std::string>& aliases() const override { return aliases_; }
  const std::string& version() const override { return version_; };
  const Protobuf::Message& resource() const override { return *resource_; };
  bool hasResource() const override { return has_resource_; }
  std::optional<std::chrono::milliseconds> ttl() const override { return ttl_; }
  const OptRef<const envoy::config::core::v3::Metadata> metadata() const override {
    return metadata_.has_value() ? makeOptRef(metadata_.value()) : std::nullopt;
  }

private:
  DecodedResourceImpl(OpaqueResourceDecoder& resource_decoder, std::optional<std::string> name,
                      const Protobuf::RepeatedPtrField<std::string>& aliases,
                      const Protobuf::Any& resource, bool has_resource, const std::string& version,
                      std::optional<std::chrono::milliseconds> ttl,
                      const std::optional<envoy::config::core::v3::Metadata>& metadata,
                      Protobuf::Arena& arena)
      : resource_(resource_decoder.decodeResource(resource, arena)), has_resource_(has_resource),
        name_(name ? *name : resource_decoder.resourceName(*resource_)),
        aliases_(repeatedPtrFieldToVector(aliases)), version_(version), ttl_(ttl),
        metadata_(metadata) {}

  const Protobuf::Arena::UniquePtr<Protobuf::Message> resource_;
  const bool has_resource_;
  const std::string name_;
  const std::vector<std::string> aliases_;
  const std::string version_;
  // Per resource TTL.
  const std::optional<std::chrono::milliseconds> ttl_;

  // This is the metadata info under the Resource wrapper.
  // It is intended to be consumed in the xds_config_tracker extension.
  const std::optional<envoy::config::core::v3::Metadata> metadata_;
};

struct DecodedResourcesWrapper {
  DecodedResourcesWrapper(const Protobuf::ArenaOptions& options = {})
      : arena_(std::make_unique<Protobuf::Arena>(options)) {}
  DecodedResourcesWrapper(DecodedResourcesWrapper&&) noexcept = default;
  DecodedResourcesWrapper& operator=(DecodedResourcesWrapper&& other) noexcept {
    if (this != &other) {
      refvec_.clear();
      owned_resources_.clear();
      arena_ = std::move(other.arena_);
      owned_resources_ = std::move(other.owned_resources_);
      refvec_ = std::move(other.refvec_);
    }
    return *this;
  }

  static absl::StatusOr<std::unique_ptr<DecodedResourcesWrapper>>
  create(OpaqueResourceDecoder& resource_decoder,
         const Protobuf::RepeatedPtrField<Protobuf::Any>& resources, const std::string& version) {
    std::unique_ptr<DecodedResourcesWrapper> ret =
        std::make_unique<DecodedResourcesWrapper>(resource_decoder.arenaOptions());
    for (const auto& resource : resources) {
      absl::StatusOr<DecodedResourceImplPtr> resource_or_error =
          DecodedResourceImpl::fromResource(resource_decoder, resource, version, *ret->arena_);
      RETURN_IF_NOT_OK_REF(resource_or_error.status());
      ret->pushBack(std::move(resource_or_error.value()));
    }
    return ret;
  }

  void pushBack(Config::DecodedResourcePtr&& resource) {
    owned_resources_.push_back(std::move(resource));
    refvec_.emplace_back(*owned_resources_.back());
  }

  std::unique_ptr<Protobuf::Arena> arena_;
  std::vector<Config::DecodedResourcePtr> owned_resources_;
  std::vector<Config::DecodedResourceRef> refvec_;
};

} // namespace Config
} // namespace Envoy
