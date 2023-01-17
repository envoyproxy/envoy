---
name: Non-{crash,security} bug
about: Bugs which are not crashes (including asserts in debug builds), DoS or other security issue
title: ''
labels: bug,triage
assignees: ''

---

**If you are reporting *any* crash or *any* potential security issue, *do not*
open an issue in this repo. Please report the issue via emailing
envoy-security@googlegroups.com where the issue will be triaged appropriately.**

*Title*: *One line description*

*Description*:
>What issue is being seen? Describe what should be happening instead of
the bug, for example: Envoy should not crash, the expected value isn't
returned, etc.

*Repro steps*:
> Include sample requests, environment, etc. All data and inputs
required to reproduce the bug.

>**Note**: The [Envoy_collect tool](https://github.com/envoyproxy/envoy/blob/main/tools/envoy_collect/README.md)
gathers a tarball with debug logs, config and the following admin
endpoints: /stats, /clusters and /server_info. Please note if there are
privacy concerns, sanitize the data prior to sharing the tarball/pasting.

*Admin and Stats Output*:
>Include the admin output for the following endpoints: /stats,
/clusters, /routes, /server_info. For more information, refer to the
[admin endpoint documentation.](https://www.envoyproxy.io/docs/envoy/latest/operations/admin)

>**Note**: If there are privacy concerns, sanitize the data prior to
sharing.

*Config*:
>Include the config used to configure Envoy.

*Logs*:
>Include the access logs and the Envoy logs.

>**Note**: If there are privacy concerns, sanitize the data prior to
sharing.

*Call Stack*:
> If the Envoy binary is crashing, a call stack is **required**.
Please refer to the [Bazel Stack trace documentation](https://github.com/envoyproxy/envoy/tree/main/bazel#stack-trace-symbol-resolution).
