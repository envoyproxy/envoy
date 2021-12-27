#pragma once

#include <hs/hs_common.h>
#include <hs/hs_compile.h>
#include <hs/hs_runtime.h>

#include "envoy/common/regex.h"

#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/hyperscan/v3alpha/hyperscan.pb.h"
#include "contrib/envoy/extensions/hyperscan/v3alpha/hyperscan.pb.validate.h"
#include "hs/hs.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

class CompiledHyperscanMatcher : public Envoy::Regex::CompiledMatcher {
public:
  explicit CompiledHyperscanMatcher(const std::string& regex,
                                    const envoy::extensions::hyperscan::v3alpha::Hyperscan&);
  ~CompiledHyperscanMatcher() override {
    hs_free_scratch(scratch_);
    hs_free_database(database_);
  }

  // Envoy::Regex::CompiledMatcher
  bool match(absl::string_view value) const override;
  std::string replaceAll(absl::string_view, absl::string_view) const override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

private:
  hs_database_t* database_{};
  hs_scratch_t* scratch_{};

  static int eventHandler(unsigned int, unsigned long long, unsigned long long, unsigned int,
                          void* context);
};

class CompiledHyperscanMatcherFactory : public Envoy::Regex::CompiledMatcherFactory {
public:
  std::string name() const override { return "hyperscan"; }

  Envoy::Regex::CompiledMatcherFactoryCb
  createCompiledMatcherFactoryCb(const Protobuf::Message& config,
                                 ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& typed_config =
        MessageUtil::downcastAndValidate<const envoy::extensions::hyperscan::v3alpha::Hyperscan&>(
            config, validation_visitor);

    return [typed_config](const std::string& regex) {
      return std::make_unique<CompiledHyperscanMatcher>(regex, typed_config);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::hyperscan::v3alpha::Hyperscan>();
  }
};

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
