.. _operations_certificates:

Certificate Management
======================

Envoy provides several mechanisms for cert management. At a high level they can be broken into

1. Static :ref:`CommonTlsContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.commontlscontext>` referenced certificates.
   These will *not* reload automatically, and requires either a restart of the proxy or
   reloading the clusters/listeners that reference them.
   :ref:`Hot restarting <arch_overview_hot_restart>` can be used here to pick up the new
   certificates without dropping traffic.
2. :ref:`Secret Discovery Service <config_secret_discovery_service>` referenced certificates.
   By using SDS, certificates can either be referenced as files (reloading the certs when the
   parent directory is moved) or through an external SDS server that can push new certificates.
