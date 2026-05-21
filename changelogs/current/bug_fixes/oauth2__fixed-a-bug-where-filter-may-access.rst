Fix: `CVE-2026-48090 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-3cj2-c63f-q26f>`_

Fixed a bug where the asyncronous token change callback could be triggered after the filter had been
torn down (``onDestroy()`` had been called), which could lead to access dangling pointers and result
in UAF/crash.
