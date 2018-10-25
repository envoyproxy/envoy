#pragma once

#include <iostream>
#include <memory>

namespace Envoy {
namespace Http {
/**
 * Pretty print const HeaderMapImpl&
 */
class HeaderMapImpl;
void PrintTo(const HeaderMapImpl& headers, std::ostream* os);

/**
 * Pretty print const HeaderMapPtr&
 */
class HeaderMap;
typedef std::unique_ptr<HeaderMap> HeaderMapPtr;
void PrintTo(const HeaderMap& headers, std::ostream* os);
void PrintTo(const HeaderMapPtr& headers, std::ostream* os);
} // namespace Http

namespace Buffer {
/**
 * Pretty print const Instance&
 */
class Instance;
void PrintTo(const Instance& buffer, std::ostream* os);

/**
 * Pretty print const Buffer::OwnedImpl&
 */
class OwnedImpl;
void PrintTo(const OwnedImpl& buffer, std::ostream* os);
} // namespace Buffer
} // namespace Envoy
