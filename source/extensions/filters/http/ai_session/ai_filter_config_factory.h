#pragma once

#include <memory>
#include <string>

#include "envoy/config/typed_config.h"

#include "source/extensions/filters/http/ai_session/ai_session_manager.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

/**
 * Base factory for inner AI filters (analogous to NamedHttpFilterConfigFactory).
 *
 * Each implementation registers one AI filter type under a unique name in the
 * "envoy.ai_filters" category.  AiSessionFilterFactory (config.cc) iterates
 * the AiSession.ai_filters list, resolves each entry here via type URL, and
 * calls createAiFilterFactory() with the deserialized config proto.
 *
 * Authoring a new AI filter:
 *   1. Subclass AiStreamFilter — implement the JSON-RPC event handlers.
 *   2. Write a proto for the filter config.
 *   3. Subclass NamedAiFilterConfigFactory — implement name(),
 *      createEmptyConfigProto(), and createAiFilterFactory().
 *   4. Register:  REGISTER_FACTORY(MyFilterFactory, NamedAiFilterConfigFactory);
 */
class NamedAiFilterConfigFactory : public Config::TypedFactory {
public:
  /**
   * Create the AiFilterFactory for this filter type.
   *
   * The returned lambda is called once per JSON-RPC request to produce a
   * fresh AiStreamFilter — analogous to a FilterFactoryCb being called once
   * per HTTP request to produce a StreamFilter.
   *
   * @param proto_config  The deserialized filter config proto.  Down-cast
   *                      to the concrete config type via dynamic_cast or
   *                      MessageUtil::downcastAndValidate.
   */
  virtual AiFilterFactory
  createAiFilterFactory(const Protobuf::Message& proto_config) = 0;

  std::string category() const override { return "envoy.ai_filters"; }
};

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
