Issue Template
*<Title>: <One line description>*

*Description:* <Describe the issue. Please be detailed.
If a feature request, please describe the desired behaviour, what 
scenario it enables and how it would be used.>

[optional *Relevant Links:* <Any extra documentation required to 
understand what is asked.>]

Bug Template
*<Title>: <One line description>*

*Description:* <What issue is being seen? Describe what should be happening instead of 
the bug, ex not a crash, a value should be returned, etc.>

*Repro steps:* <Include sample requests, environment, etc All data 
required to reproduce the bug.>

Note: The [envoy_collect tool](https://github.com/envoyproxy/envoy/blob/master/tools/envoy_collect/README.md)
gathers a tarball with debug logs, config and the following admin 
endpoints: /stats, /clusters and /server_info. Please note if there are
privacy concerns, sanitize the data prior to sharing the tarball/pasting. 

*Admin and Stats Output:* <Include the admin output for the following
endpoints: /stats, /clusters, /routes, /server_info. For more 
information, refer to the [admin endpoint documentation.](https://envoyproxy.github.io/envoy/operations/admin.html)>

*Config:* <Include the config used to configure Envoy.>

*Logs:* <Include the access logs and the envoy logs>

*Call Stack:* <If the envoy binary is crashing, a call stack is required.
Please refer to the [Bazel Stack trace documentation](https://github.com/envoyproxy/envoy/tree/master/bazel#stack-trace-symbol-resolution) >
