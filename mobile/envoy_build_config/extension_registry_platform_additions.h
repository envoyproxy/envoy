#pragma once

namespace Envoy {
class ExtensionRegistryPlatformAdditions {
public:
  // As a server, Envoy's static factory registration happens when main is run. However, when
  // compiled as a library, there is no guarantee that such registration will happen before the
  // names are needed. The following calls ensure that registration happens before the entities are
  // needed. Note that as more registrations are needed, explicit initialization calls will need to
  // be added here.
  static void registerFactories();
};
} // namespace Envoy
