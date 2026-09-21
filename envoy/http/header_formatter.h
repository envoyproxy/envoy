#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/config/typed_config.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class GenericFactoryContext;
} // namespace Configuration
} // namespace Server

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
  [[deprecated("Use createFactoryFromProto instead")]]
  virtual StatefulHeaderKeyFormatterFactorySharedPtr
  createFromProto(const Protobuf::Message& /* config */) {
    // This is the terminal of the delegation below, so it must not delegate back: an extension
    // that implements neither method lands here.
    throwEnvoyExceptionOrPanic(
        "Stateful header key formatter factory implements neither createFactoryFromProto nor the "
        "deprecated createFromProto");
    return nullptr;
  }

  /**
   * Create a factory from the given configuration. This is the method Envoy calls, and the one new
   * extensions should implement.
   *
   * The factory is created on the main thread, but the formatters it produces are used - and
   * released - by worker threads, so an implementation owning state that must not be torn down off
   * the main thread needs the context (specifically
   * serverFactoryContext().mainThreadDispatcher()) to schedule its own destruction.
   *
   * @param config the extension specific configuration.
   * @param context the factory context of whatever owns the protocol options - a listener for the
   * downstream connection manager, a cluster for upstream protocol options - which is why this is
   * the generic context rather than a listener-scoped one.
   * @return the formatter factory, or an error status if the configuration is invalid.
   */
  virtual absl::StatusOr<StatefulHeaderKeyFormatterFactorySharedPtr>
  createFactoryFromProto(const Protobuf::Message& config,
                         Server::Configuration::GenericFactoryContext&) {
    // Delegate to the legacy entry point so that extensions which only implement it - out-of-tree
    // extensions written before the context was threaded through - keep working.
    return createFromProto(config);
  }

  std::string category() const override { return "envoy.http.stateful_header_formatters"; }
};

} // namespace Http
} // namespace Envoy
