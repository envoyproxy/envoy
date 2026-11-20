``Server::Configuration::BootstrapExtensionFactory::createBootstrapExtension`` now returns
``absl::StatusOr<BootstrapExtensionPtr>`` instead of throwing on configuration errors, matching the
non-throwing pattern already used by HTTP/network filter factories. This is a source-breaking change
for out-of-tree bootstrap extensions: the interface method is now pure virtual with no default
implementation, so every ``BootstrapExtensionFactory`` implementation must be updated to return a
status instead of throwing.
