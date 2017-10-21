**Issue Template**
&lt;Title&gt;: &lt;One line description&gt;

*Description:* &lt;Describe the issue. Please be detailed.
If a feature request, please describe the desired behaviour, what 
scenario it enables and how it would be used.&gt;

[optional *Relevant Links:* &lt;Any extra documentation required to 
understand what is asked.>]

**Bug Template**
&lt;Title&gt;: &lt;One line description&gt;

*Description:* &lt;What issue is being seen? Describe what should be 
happening instead of the bug, ex not a crash, a value should be returned,
etc.&gt;

*Repro steps:* &lt;Include sample requests, environment, etc All data 
required to reproduce the bug.&gt;

Note: The [envoy_collect tool](https://github.com/envoyproxy/envoy/blob/master/tools/envoy_collect/README.md)
gathers a tarball with debug logs, config and the following admin 
endpoints: /stats, /clusters and /server_info. Please note if there are
privacy concerns, sanitize the data prior to sharing the tarball/pasting. 

*Admin and Stats Output:* &lt;Include the admin output for the following
endpoints: /stats, /clusters, /routes, /server_info. For more 
information, refer to the [admin endpoint documentation.](https://envoyproxy.github.io/envoy/operations/admin.html)&gt;

*Config:* &lt;Include the config used to configure Envoy.&gt;

*Logs:* &lt;Include the access logs and the envoy logs.&gt;

*Call Stack:* &lt;If the envoy binary is crashing, a call stack is required.
Please refer to the [Bazel Stack trace documentation](https://github.com/envoyproxy/envoy/tree/master/bazel#stack-trace-symbol-resolution).&gt;
