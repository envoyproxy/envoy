#pragma once

#include "envoy/tracing/custom_tag.h"

namespace Envoy {
namespace Tracing {

constexpr uint32_t DefaultMaxPathTagLength = 256;

enum class OperationName { Ingress, Egress };

/**
 * Tracing configuration, it carries additional data needed to populate the span.
 */
class Config {
public:
  virtual ~Config() = default;

  /**
   * @return operation name for tracing, e.g., ingress.
   */
  virtual OperationName operationName() const PURE;

  /**
   * @return custom tags to be attached to the active span.
   */
  virtual const CustomTagMap* customTags() const PURE;

  /**
   * @return true if spans should be annotated with more detailed information.
   */
  virtual bool verbose() const PURE;

  /**
   * @return the maximum length allowed for paths in the extracted HttpUrl tag. This is only used
   * for HTTP protocol tracing.
   */
  virtual uint32_t maxPathTagLength() const PURE;
};

} // namespace Tracing
} // namespace Envoy
