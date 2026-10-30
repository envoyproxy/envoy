``Server::Configuration::BootstrapExtensionFactory::createBootstrapExtension`` is deprecated in
favor of ``createBootstrapExtensionOrError``, which returns ``absl::StatusOr`` instead of throwing
on configuration errors. Bootstrap extensions that have not yet migrated keep working unmodified
via the default implementation of the new method.
