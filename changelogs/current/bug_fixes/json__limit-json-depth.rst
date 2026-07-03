Fix: `CVE-2026-48042 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-f24p-rxw2-g6pv>`_

Limit JSON nesting depth at 1000. The limit could be relaxed to 10K by setting the
``envoy.reloadable_features.limit_json_parser_nesting_depth`` to ``false``.
