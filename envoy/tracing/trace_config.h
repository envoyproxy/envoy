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
   * @return create separated child span for upstream request if true.
   */
  virtual bool spawnUpstreamSpan() const PURE;

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

/**
 * Route or connection manager level tracing configuration.
 */
class TracingConfig {
public:
  virtual ~TracingConfig() = default;

  /**
   * This method returns the client sampling percentage.
   * @return the client sampling percentage
   */
  virtual const envoy::type::v3::FractionalPercent& getClientSampling() const PURE;

  /**
   * This method returns the random sampling percentage.
   * @return the random sampling percentage
   */
  virtual const envoy::type::v3::FractionalPercent& getRandomSampling() const PURE;

  /**
   * This method returns the overall sampling percentage.
   * @return the overall sampling percentage
   */
  virtual const envoy::type::v3::FractionalPercent& getOverallSampling() const PURE;

  /**
   * This method returns the tracing custom tags.
   * @return the tracing custom tags.
   */
  virtual const Tracing::CustomTagMap& getCustomTags() const PURE;
};

/**
 * Connection manager tracing configuration.
 */
class ConnectionManagerTracingConfig : public TracingConfig {
public:
  /**
   * @return operation name for tracing, e.g., ingress.
   */
  virtual OperationName operationName() const PURE;

  /**
   * @return create separated child span for upstream request if true.
   */
  virtual bool spawnUpstreamSpan() const PURE;

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

using ConnectionManagerTracingConfigPtr = std::unique_ptr<ConnectionManagerTracingConfig>;

} // namespace Tracing
} // namespace Envoy
