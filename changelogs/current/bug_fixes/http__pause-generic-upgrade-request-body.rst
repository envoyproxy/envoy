Fix: `CVE-2026-73548 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-3vhp-c83q-jqc2>`_.

Fixed a vulnerability where payload sent before a generic HTTP upgrade was accepted could be
interpreted as a pipelined HTTP/1 request and poison a shared upstream connection. Generic upgrade
payload is now paused until the upstream accepts the upgrade. This change can be temporarily
reverted by setting ``envoy.reloadable_features.http_pause_generic_upgrade_request_body`` to
``false``.
