dynamic modules: hardened the handling of two module-provided values that Envoy reads after the
producing hook returns. The ``on_program_init`` version string is now copied out of module memory
right away, and the ABI documentation for the version string and the cert validator digest buffer
now states that they must stay valid until Envoy reads them immediately after the hook returns.
