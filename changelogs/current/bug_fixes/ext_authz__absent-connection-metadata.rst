Hardened the ``ext_authz`` filter to avoid a crash when building the metadata context for a stream
that has no downstream connection (for example when the filter runs in an upstream filter chain driven
by the async client). Connection metadata is now included only when a connection is present.
