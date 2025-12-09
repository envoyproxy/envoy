**Summary of changes**:

* Security updates:

  Resolve dependency CVEs:
  - c-ares/CVE-2025-0913:
      Use after free can crash Envoy due to malfunctioning or compromised DNS.

While a potentially severe bug in some cloud environments, this has limited exploitability
as any attacker would require control of DNS.

Envoy advisory is here https://github.com/envoyproxy/envoy/security/advisories/GHSA-fg9g-pvc4-776f
