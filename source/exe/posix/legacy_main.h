#pragma once

#include "server/options_impl.h"

namespace Envoy {

/**
 * Legacy implementation of main_common.
 *
 * TODO(jmarantz): Remove this when all callers are removed. At that time, MainCommonBase
 * and MainCommon can be merged. The current theory is that only Google calls this.
 *
 * @param options Options object initialized by site-specific code
 * @return int Return code that should be returned from the actual main()
 */
int main_common(OptionsImpl& options);

} // namespace Envoy
