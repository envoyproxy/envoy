#pragma once

#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/dispatcher_thread_deletable.h"
#include "envoy/http/header_formatter.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnHeaderFormatterConfigNewType =
    decltype(&envoy_dynamic_module_on_header_formatter_config_new);
using OnHeaderFormatterConfigDestroyType =
    decltype(&envoy_dynamic_module_on_header_formatter_config_destroy);
using OnHeaderFormatterNewType = decltype(&envoy_dynamic_module_on_header_formatter_new);
using OnHeaderFormatterDestroyType = decltype(&envoy_dynamic_module_on_header_formatter_destroy);
using OnHeaderFormatterProcessKeyType =
    decltype(&envoy_dynamic_module_on_header_formatter_process_key);
using OnHeaderFormatterFormatType = decltype(&envoy_dynamic_module_on_header_formatter_format);

class DynamicModuleHeaderFormatterConfig;
using DynamicModuleHeaderFormatterConfigSharedPtr =
    std::shared_ptr<DynamicModuleHeaderFormatterConfig>;

/**
 * StatefulHeaderKeyFormatter implementation that delegates header key casing to a dynamic module.
 */
class DynamicModuleHeaderFormatter : public Envoy::Http::StatefulHeaderKeyFormatter {
public:
  explicit DynamicModuleHeaderFormatter(DynamicModuleHeaderFormatterConfigSharedPtr config);

  ~DynamicModuleHeaderFormatter() override;

  /**
   * Creates the in-module formatter instance backing this object. This is separate from the
   * constructor because the module is handed the pointer to this object, so this object must
   * already exist when the module is called.
   * @return false if the module declined to create a formatter. The caller must then discard this
   * object, which leaves Envoy using its default header casing for the message.
   */
  bool initializeInModuleFormatter();

  // Envoy::Http::HeaderKeyFormatter
  std::string format(absl::string_view key) const override;

  // Envoy::Http::StatefulHeaderKeyFormatter
  void processKey(absl::string_view key) override;
  void setReasonPhrase(absl::string_view reason_phrase) override;
  absl::string_view getReasonPhrase() const override;

private:
  void* thisAsVoidPtr() const {
    return static_cast<void*>(const_cast<DynamicModuleHeaderFormatter*>(this));
  }

  const DynamicModuleHeaderFormatterConfigSharedPtr config_;
  // Null until initializeInModuleFormatter() succeeds, and never reset afterwards.
  envoy_dynamic_module_type_header_formatter_module_ptr in_module_formatter_{nullptr};
};

/**
 * Configuration for a dynamic module header formatter, which doubles as the factory Envoy holds in
 * the HTTP/1 protocol options.
 */
class DynamicModuleHeaderFormatterConfig
    : public Envoy::Http::StatefulHeaderKeyFormatterFactory,
      public Event::DispatcherThreadDeletable,
      public std::enable_shared_from_this<DynamicModuleHeaderFormatterConfig>,
      public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  explicit DynamicModuleHeaderFormatterConfig(
      Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleHeaderFormatterConfig() override;

  // Envoy::Http::StatefulHeaderKeyFormatterFactory
  //
  // Returns nullptr when the module declines to create a formatter, which makes the codec use
  // Envoy's default header casing for that message rather than failing it.
  Envoy::Http::StatefulHeaderKeyFormatterPtr create() override;

  // The loaded module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  // The corresponding in-module header formatter configuration.
  envoy_dynamic_module_type_header_formatter_config_module_ptr in_module_config_{nullptr};

  // The function pointers resolved from the module. All are guaranteed non-nullptr after
  // newDynamicModuleHeaderFormatterConfig() succeeds.
  OnHeaderFormatterConfigDestroyType on_config_destroy_{nullptr};
  OnHeaderFormatterNewType on_formatter_new_{nullptr};
  OnHeaderFormatterDestroyType on_formatter_destroy_{nullptr};
  OnHeaderFormatterProcessKeyType on_process_key_{nullptr};
  OnHeaderFormatterFormatType on_format_{nullptr};
};

/**
 * Creates a new DynamicModuleHeaderFormatterConfig for the given configuration. Must be called on
 * the main thread.
 * @param formatter_name the name selecting an implementation inside the module. It is only handed
 * to the module for the duration of its config_new hook, not retained here.
 * @param formatter_config the configuration bytes for the header formatter, borrowed for the same
 * duration.
 * @param dynamic_module the dynamic module to use.
 * @param main_thread_dispatcher the main thread dispatcher, used to destroy the returned
 * configuration - and with it the module - on the main thread no matter which thread drops the
 * last reference. It must outlive the configuration, which captures a reference to it. The server
 * owns the main dispatcher for the whole process lifetime, so that holds for any configuration
 * reachable from a listener or a cluster.
 * @return the new configuration, a NotFoundError when a required ABI symbol is missing, or an
 * InvalidArgumentError when the in-module configuration failed to initialize.
 */
absl::StatusOr<DynamicModuleHeaderFormatterConfigSharedPtr>
newDynamicModuleHeaderFormatterConfig(absl::string_view formatter_name,
                                      absl::string_view formatter_config,
                                      Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                      Event::Dispatcher& main_thread_dispatcher);

} // namespace DynamicModules
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
