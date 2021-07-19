#include "source/common/common/envoy_defaults.h"

const Envoy::DefaultsProfile& getDefaultsProfile() {
  if (Envoy::DefaultsProfileSingleton::getExisting()) {
    return Envoy::DefaultsProfileSingleton::get();
  }

  static Envoy::DefaultsProfile no_singleton_profile_;
  return no_singleton_profile_;
};

const Envoy::DefaultsProfile& Envoy::DefaultsProfile::get() {
  if (Envoy::DefaultsProfileSingleton::getExisting()) {
    return Envoy::DefaultsProfileSingleton::get();
  }

  static Envoy::DefaultsProfile no_singleton_profile_;
  return no_singleton_profile_;
};
