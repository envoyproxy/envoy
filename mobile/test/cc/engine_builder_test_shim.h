#pragma once

#if defined(USE_MOBILE_ENGINE_BUILDER)

#include "library/cc/mobile_engine_builder.h"

namespace Envoy {
namespace Platform {
class EngineBuilder : public MobileEngineBuilder {
public:
  using MobileEngineBuilder::MobileEngineBuilder;

  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> generateBootstrap() const {
    auto* mutable_this = const_cast<EngineBuilder*>(this);
    return mutable_this->Platform::EngineBuilderBase<MobileEngineBuilder>::generateBootstrap()
        .value();
  }
};
using EngineBuilderSharedPtr = std::shared_ptr<EngineBuilder>;
} // namespace Platform
} // namespace Envoy

#else

#include "library/cc/engine_builder.h"

#endif
