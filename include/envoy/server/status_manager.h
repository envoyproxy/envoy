#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

/* A class to track the status of reloadable components */
class StatusManager {
public:
  struct StatusHandle {
    StatusHandle(bool success, std::string details) : success_(success), details_(details) {}
    // True if the last load of the component was successful, false otherwise.
    bool success_;
    // Details about the last status load.
    std::string details_;
  };

  virtual ~StatusManager() {}

  /**
   * Adds a new component to track status of.
   *
   * @param component supplies the component for the StatusManager to track
   * updateStatus() and status() should only be called for components which have
   * been added in this way
   */
  virtual void addComponent(absl::string_view component) PURE;

  /**
   * Updates the status of a given component.
   *
   * @param component supplies the component to update the status of
   * @param details supplies the latest status for the supplied component
   */
  virtual void updateStatus(absl::string_view component,
                            std::unique_ptr<StatusHandle>&& details) PURE;

  /**
   * Returns the status of a given component
   *
   * @param component supplies the component to return the status of
   * @return StatusHandle the status of the component supplied
   */
  virtual StatusHandle status(absl::string_view component) PURE;
};

} // namespace Server
} // namespace Envoy
