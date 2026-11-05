#pragma once

#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Event {

/**
 * @return the worker id when 'thread_name' is the name of a worker thread's dispatcher
 *         ('worker_<id>', the well-known form the listener manager names its workers with),
 *         std::nullopt otherwise.
 */
std::optional<uint32_t> workerIdFromThreadName(absl::string_view thread_name);

} // namespace Event
} // namespace Envoy
