Fix: `CVE-2026-47220 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-j9wh-4qfm-wf2v>`_

Fixed a crash bug in the ``%REQUESTED_SERVER_NAME%`` formatter where the host or original host is not set
correctly but the formatter is configured to access the host value.
