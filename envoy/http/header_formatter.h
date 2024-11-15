#pragma once

#include "envoy/common/optref.h"
#include "envoy/config/typed_config.h"

namespace Envoy {
namespace Http {

/**
 * Interface for generic header key formatting.
 */
class HeaderKeyFormatter {
public:
  virtual ~HeaderKeyFormatter() = default;

  /**
   * Given an input key return the formatted key to encode.
   */
  virtual std::string format(absl::string_view key) const PURE;
};

using HeaderKeyFormatterConstPtr = std::unique_ptr<const HeaderKeyFormatter>;
using HeaderKeyFormatterOptConstRef = OptRef<const HeaderKeyFormatter>;

/**
 * Interface for header key formatters that are stateful. A formatter is created during decoding
 * headers, attached to the header map, and can then be used during encoding for reverse
 * translations if applicable.
 */
class StatefulHeaderKeyFormatter : public HeaderKeyFormatter {
public:
  /**
   * Called for each header key received by the codec.
   */
  virtual void processKey(absl::string_view key) PURE;

  /**
   * Called to save received reason phrase
   */
  virtual void setReasonPhrase(absl::string_view reason_phrase) PURE;

  /**
   * Called to get saved reason phrase
   */
  virtual absl::string_view getReasonPhrase() const PURE;
};

using StatefulHeaderKeyFormatterPtr = std::unique_ptr<StatefulHeaderKeyFormatter>;
using StatefulHeaderKeyFormatterOptRef = OptRef<StatefulHeaderKeyFormatter>;
using StatefulHeaderKeyFormatterOptConstRef = OptRef<const StatefulHeaderKeyFormatter>;

/**
 * Interface for creating stateful header key formatters.
 */
class StatefulHeaderKeyFormatterFactory {
public:
  virtual ~StatefulHeaderKeyFormatterFactory() = default;

  /**
   * Create a new formatter.
   */
  virtual StatefulHeaderKeyFormatterPtr create() PURE;
};

using StatefulHeaderKeyFormatterFactorySharedPtr =
    std::shared_ptr<StatefulHeaderKeyFormatterFactory>;

/**
 * Extension configuration for stateful header key formatters.
 */
class StatefulHeaderKeyFormatterFactoryConfig : public Config::TypedFactory {
public:
  virtual StatefulHeaderKeyFormatterFactorySharedPtr
  createFromProto(const Protobuf::Message& config) PURE;

  std::string category() const override { return "envoy.http.stateful_header_formatters"; }
};

} // namespace Http
} // namespace Envoy
