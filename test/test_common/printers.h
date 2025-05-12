#pragma once

#include <iostream>
#include <memory>

#include "envoy/network/address.h"

namespace Envoy {
namespace Http {
/**
 * Pretty print const HeaderMapImpl&
 */
class HeaderMapImpl;
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const HeaderMapImpl& headers, std::ostream* os);

/**
 * Pretty print const HeaderMapPtr&
 */
class HeaderMap;
using HeaderMapPtr = std::unique_ptr<HeaderMap>;
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const HeaderMap& headers, std::ostream* os);
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const HeaderMapPtr& headers, std::ostream* os);
} // namespace Http

namespace Buffer {
/**
 * Pretty print const Instance&
 */
class Instance;
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const Instance& buffer, std::ostream* os);

/**
 * Pretty print const Buffer::OwnedImpl&
 */
class OwnedImpl;
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const OwnedImpl& buffer, std::ostream* os);
} // namespace Buffer

namespace Network {
namespace Address {
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const Instance& address, std::ostream* os);
} // namespace Address
} // namespace Network
} // namespace Envoy
