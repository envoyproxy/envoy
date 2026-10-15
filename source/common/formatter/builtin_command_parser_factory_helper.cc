#include "source/common/formatter/builtin_command_parser_factory_helper.h"

#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"

namespace Envoy {
namespace Formatter {

const BuiltInCommandParserFactoryHelper::Parsers&
BuiltInCommandParserFactoryHelper::commandParsers() {
  CONSTRUCT_ON_FIRST_USE(Parsers, []() {
    BuiltInCommandParserFactoryHelper::Parsers parsers;
    for (const auto& factory : Envoy::Registry::FactoryRegistry<Factory>::factories()) {
      if (auto parser = factory.second->createCommandParser(); parser == nullptr) {
        ENVOY_BUG(false, fmt::format("Null built-in command parser: {}", factory.first));
        continue;
      } else {
        parsers.push_back(std::move(parser));
      }
    }
    return parsers;
  }());
}

} // namespace Formatter
} // namespace Envoy
