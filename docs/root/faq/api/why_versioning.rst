Why are the Envoy xDS APIs versioned? What is the benefit?
==========================================================

Envoy is a platform and needs to allow its APIs to grow and evolve to encompass new features,
improve ergonomics and address new use cases. At the same time, we need a disciplined approach to
turning down stale functionality and removing APIs and their supporting code that are no longer
maintained. If we don't do this, we lose the ability in the long term to provide a reliable,
maintainable, scalable code base and set of APIs for our users.

We had previously put in place policies around :repo:`breaking changes
<CONTRIBUTING.md#breaking-change-policy>` across releases, the :repo:`API versioning policy
<api/API_VERSIONING.md>` takes this a step further, articulating a guaranteed multi-year support
window for APIs that provides control plane authors a predictable clock when considering support
for a range of Envoy versions.
