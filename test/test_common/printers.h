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
void printTo(const HeaderMapImpl& headers, std::ostream* os);

/**
 * Pretty print const HeaderMapPtr&
 */
class HeaderMap;
using HeaderMapPtr = std::unique_ptr<HeaderMap>;
void printTo(const HeaderMap& headers, std::ostream* os);
void printTo(const HeaderMapPtr& headers, std::ostream* os);
} // namespace Http

namespace Buffer {
/**
 * Pretty print const Instance&
 */
class Instance;
void printTo(const Instance& buffer, std::ostream* os);

/**
 * Pretty print const Buffer::OwnedImpl&
 */
class OwnedImpl;
void printTo(const OwnedImpl& buffer, std::ostream* os);
} // namespace Buffer

namespace Network {
namespace Address {
void printTo(const Instance& address, std::ostream* os);
}
} // namespace Network
} // namespace Envoy
