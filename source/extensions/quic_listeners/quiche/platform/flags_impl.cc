// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/flags_impl.h"

#include "envoy/registry/registry.h"

#include "server/options_impl.h"

namespace quiche {

namespace {

class QuicheFlagProvider : public Envoy::CommandLineFlagProvider {
public:
  void AddFlags(TCLAP::CmdLineInterface& cmdline) override { RegisterFlags(cmdline); }
  std::string name() override { return "com.googlesource.quiche"; }
};

Envoy::Registry::RegisterFactory<QuicheFlagProvider, Envoy::CommandLineFlagProvider> registered;

} // namespace

// Flag definitions
#define QUICHE_FLAG(type, flag, value, doc)                                                        \
  SettableValueArg<type> FLAGS_##flag(/*flag=*/"", /*name=*/#flag, /*desc=*/doc, /*req=*/false,    \
                                      /*value=*/value, /*typeDesc=*/#type);
#include "extensions/quic_listeners/quiche/platform/flags_list.h"
#undef QUICHE_FLAG

void RegisterFlags(TCLAP::CmdLineInterface& cmdline) {
#define QUICHE_FLAG(type, flag, value, doc) cmdline.add(FLAGS_##flag);
#include "extensions/quic_listeners/quiche/platform/flags_list.h"
#undef QUICHE_FLAG
}

void ResetFlags() {
#define QUICHE_FLAG(type, flag, value, doc) FLAGS_##flag.reset();
#include "extensions/quic_listeners/quiche/platform/flags_list.h"
#undef QUICHE_FLAG
}

} // namespace quiche
