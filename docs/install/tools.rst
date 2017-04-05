.. _install_tools:

Router Table Check Tool
=======================

The router check tool checks whether the route parameters returned by a router match what is expected.
The tool can also be used to check whether a path redirect, path rewrite, or host rewrite
match what is expected.

Input
  The tool expects two input JSON files:

  1. A router config JSON file. The router config JSON file schema is found :ref:`here <config_http_conn_man_route_table>`.

  2. A tool config JSON file. The tool config JSON file schema is found :ref:`here <config_tools_router_check_tool>`.
     The tool config input file specifies urls (composed of authorities and paths)
     and expected route parameter values. Additonal parameters such as additonal headers are optional.

Output
  The program exits with status EXIT_FAILURE if any test case does not match the expected route parameter
  value.

  The - -details option prints out details for each test case. The first field indicates
  a P for a correct match and a F for a failed match. The second field is the actual route parameter value.
  The third field is the expected route parameter value. For example: ::

    P -----api.lyft.com ----- api.lyft.com
    F -----ats ----- locations
    P -----locations ----- locations
    F -----www2 ----- www3
    P -----root_www2 ----- root_www2
    P -----https://redirect.lyft.com/new_bar ----- https://redirect.lyft.com/new_bar

  Testing with valid runtime values is not currently supported but can be added.

Building
  The tool can be built locally using Bazel. ::

    bazel build //test/tools/router_check:router_check_tool

Running
  The tool takes two input json files and an optional command line parameter - -details. The
  expected order of command line arguements is:
  1. The router configuration json file.
  2. The tool configuration json file.
  3. The optional details flag. ::

    bazel-bin/test/tools/router_check/router_check_tool router_config.json tool_config.json

    bazel-bin/test/tools/router_check/router_check_tool router_config.json tool_config.json --details

Testing
  A bash shell script test can be run with bazel. The test compares routes using different router and
  tool configuration json files. The configuration json files can be found in
  test/tools/router_check/test/config/... . ::

    bazel test //test/tools/router_check/...
