If I upgrade to Envoy 1.13+, do I need to use the v3 API?
=========================================================

The v2 API is deprecated in the 1.13.0 release (January 2020). It will be fully supported for the
duration of 2020 and then all support for v2 will be removed from Envoy at EOY 2020.

All existing v2 boostrap and xDS configuration should continue to work seamlessly in 1.13.0 and for the
duration of 2020. Envoy internally operates at v3+, but does so by transparently upgrading
configuration from v2 at ingestion time.

Since EOQ1 2020, we have frozen the v2 API and no new features will be added. To consume these
newer features, you will need to migrate to the v3 API.

It is highly recommend that operators with self-managed and/or self-developed control planes migrate
to v3 well before Q4 2020 in order to avoid hitting the hard deadline for v3 support at EOY.

