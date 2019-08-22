#pragma once

#include <memory>

#include "envoy/common/matchers.h"

namespace Envoy {
namespace Regex {

/**
 * A compiled regex expression matcher which uses an abstract regex engine.
 *
 * NOTE: Currently this is the same as StringMatcher, however has been split out as in the future
 *       we are likely to add other methods such as returning captures, etc.
 */
class CompiledMatcher : public Matchers::StringMatcher {};

using CompiledMatcherPtr = std::unique_ptr<const CompiledMatcher>;

} // namespace Regex
} // namespace Envoy
