#pragma once

#include "envoy/sse_content_parser/parser.h"

namespace Envoy {
namespace SseContentParser {

/**
 * Factory for creating SSE content parser instances. Provides parser metadata
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
   *
   * This prefix is used to namespace parser-specific metrics, allowing each parser
   * implementation to have its own set of metrics. The filter will prepend
   * "sse_to_metadata.resp." and append metric names to create the full metric path.
   *
   * For example, if statsPrefix() returns "json.", the full metric paths will be:
   * - sse_to_metadata.resp.json.metadata_added
   * - sse_to_metadata.resp.json.metadata_from_fallback
   * - sse_to_metadata.resp.json.parse_error
   * - sse_to_metadata.resp.json.selector_not_found
   * - etc.
   *
   * @return const std::string& the stats prefix with trailing dot (e.g., "json.")
   */
  virtual const std::string& statsPrefix() const PURE;
};

using ParserFactoryPtr = std::unique_ptr<ParserFactory>;

} // namespace SseContentParser
} // namespace Envoy
