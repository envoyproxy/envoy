import "cve_utils" as Utils;

Utils::deps_list($deps[0]) as $deps
| Utils::iterate_cves(.vulnerabilities; $deps; $ignored[0])
