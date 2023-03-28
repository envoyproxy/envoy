#include "contrib/hyperscan/matching/input_matchers/source/config.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"

#ifndef HYPERSCAN_DISABLED
#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

Envoy::Matcher::InputMatcherFactoryCb
Config::createInputMatcherFactoryCb(const Protobuf::Message& config,
                                    Server::Configuration::ServerFactoryContext& factory_context) {
  const auto hyperscan_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan&>(
      config, factory_context.messageValidationVisitor());

#ifdef HYPERSCAN_DISABLED
  throw EnvoyException("X86_64 architecture is required for Hyperscan.");
#else
  // Hyperscan's API requires vectors of expressions, flags and IDs for matching database
  // compilation.
  return [hyperscan_config, &factory_context]() {
    int size = hyperscan_config.regexes().size();
    std::vector<const char*> expressions;
    std::vector<unsigned int> flags;
    std::vector<unsigned int> ids;
    expressions.reserve(size);
    flags.reserve(size);
    ids.reserve(size);
    for (const auto& regex : hyperscan_config.regexes()) {
      expressions.push_back(regex.regex().c_str());
      unsigned int flag = 0;
      if (regex.caseless()) {
        flag |= HS_FLAG_CASELESS;
      }
      if (regex.dot_all()) {
        flag |= HS_FLAG_DOTALL;
      }
      if (regex.multiline()) {
        flag |= HS_FLAG_MULTILINE;
      }
      if (regex.allow_empty()) {
        flag |= HS_FLAG_ALLOWEMPTY;
      }
      if (regex.utf8()) {
        flag |= HS_FLAG_UTF8;
        if (regex.ucp()) {
          flag |= HS_FLAG_UCP;
        }
      }
      if (regex.combination()) {
        flag |= HS_FLAG_COMBINATION;
      }
      if (regex.quiet()) {
        flag |= HS_FLAG_QUIET;
      }
      flags.push_back(flag);
      ids.push_back(regex.id());
    }

    return std::make_unique<Matcher>(expressions, flags, ids,
                                     factory_context.mainThreadDispatcher(),
                                     factory_context.threadLocal(), false);
  };
#endif
}

REGISTER_FACTORY(Config, Envoy::Matcher::InputMatcherFactory);

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
