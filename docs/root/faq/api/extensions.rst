How does API versioning interact with a new extension?
======================================================

For extension configuration API, please follow the :repo:`new extension configuration steps
<api/STYLE.md#adding-an-extension-configuration-to-the-api>` in the style guide.

Extension implementations should operate with v3 messages internally, for both their own
configuration and other Envoy configuration messages. Unit tests should be written against v3
configuration.
