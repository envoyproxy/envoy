#pragma once

#include "envoy/config/typed_config.h"

#include "source/extensions/cache/cache_policy/cache_policy.h"

namespace Envoy {
  namespace Extensions {
    namespace Cache {

      

class CachePolicyFactory : public Config::TypedFactory {
public:
  // From UntypedFactory
  std::string category() const override { return "envoy.http.cache_policy"; }

  virtual std::unique_ptr<CachePolicy>
  createCachePolicyFromProto(const Protobuf::Message& config) PURE;
};

// A base class to handle the proto downcasting and validation to save some
// boilerplate for factory implementations.
template <class ConfigProto> class CachePolicyFactoryBase : public CachePolicyFactory {
public:
  CachePolicyPtr createCachePolicyFromProto(const Protobuf::Message& config) override {
    return createCachePolicyFromProtoTyped(
        Envoy::MessageUtil::downcastAndValidate<const ConfigProto&>(
            config, Envoy::ProtobufMessage::getStrictValidationVisitor()));
  }

protected:
  CachePolicyFactoryBase() {}

private:
  virtual CachePolicyPtr createCachePolicyFromProtoTyped(const ConfigProto& config) PURE;
};

class CachePolicyImplFactory
    : public CachePolicyFactoryBase<
          envoy::extensions::cache::cache_policy::v3::CachePolicyConfig> {
public:
  std::string name() const override { return "envoy.extensions.http.cache_policy_impl"; }
  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::cache::cache_policy::v3::CachePolicyConfig>();
  }

private:
  CachePolicyPtr
  createCachePolicyFromProtoTyped([[maybe_unused]] const envoy::extensions::cache::
                                      cache_policy::v3::CachePolicyConfig& config) override {
    return std::make_unique<CachePolicyImpl>();
  }
};

    } // namespace Cache
  } // namespace Extensions
} // namespace Envoy
