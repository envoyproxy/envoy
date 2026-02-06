#pragma once

#include "envoy/content_parser/parser.h"

namespace Envoy {
namespace ContentParser {

/**
 * Factory for creating content parser instances. Provides parser metadata
 * such as stats prefix for potential namespaced metrics.
 */
class ParserFactory {
public:
  virtual ~ParserFactory() = default;

  /**
   * Create a new Parser instance.
   * @return ParserPtr a new parser instance
   */
  virtual ParserPtr createParser() PURE;

  /**
   * Get the stats prefix for this parser type.
   * @return const std::string& the stats prefix with trailing dot (e.g., "json.")
   */
  virtual const std::string& statsPrefix() const PURE;
};

using ParserFactoryPtr = std::unique_ptr<ParserFactory>;

} // namespace ContentParser
} // namespace Envoy
