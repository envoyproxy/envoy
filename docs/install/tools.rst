.. _install_tools

Router Check Tool
=================

The router check tool checks whether the route returned by a router matches what is expected.
The tool can also be used to check whether a path redirect, path rewrite, or host rewrite
matches what is expected.

Input
-----

The tool expects two input JSON files:

1. A router config JSON file. The router config JSON file has schema
   <https://lyft.github.io/envoy/docs/configuration/http_conn_man/route_config/route_config.html#route-configuration>

2. A tool config JSON file. The tool config input file specifies urls (composed of authorities and paths)
   and expected route parameter values. Additonal parameters such as additonal headers are optional.

Output
------

The results are printed to std out. The total number of matches and conflits are printed. For example, the
following specifies 31 matches and no conflicts.::

  Total Y:31 N:0

The --details option prints out details for each url to be checked. The first field indicates
a Y for match and a N for a confict. The second field is the actual route parameter value.
The third field is the expected route parameter value. For example: ::

  Y instant-server instant-server
  Y ats ats
  Y locations locations
  N cluster1 instant-server
  N www2 none

Testing with valid runtime values is not currently supported but can be added.

Building
--------

The tool can be built locally using Bazel. ::

  bazel build //test/tools/router_check:router_check_tool

Running
-------
The tool takes two input json files and an optional command line parameter --details.
The first arguement is the router configuration json. The second arguement is the
tool configuration json. ::

  bazel-bin/test/tools/router_check/router_check_tool router_config.json tool_config.json

  bazel-bin/test/tools/router_check/router_check_tool router_config.json tool_config.json --details
