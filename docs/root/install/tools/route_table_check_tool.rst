.. _install_tools_route_table_check_tool:

Route table check tool
=======================

The route table check tool checks whether the route parameters returned by a router match what is expected.
The tool can also be used to check whether a path redirect, path rewrite, or host rewrite
match what is expected.

Input
  The tool expects two input files:

  1. A v2 router config file (YAML or JSON). The router config file schema is found in
     :ref:`config <envoy_api_file_envoy/api/v2/route/route.proto>` and the config file extension
     must reflect its file type (for instance, .json for JSON and .yaml for YAML).

  2. A tool config JSON file. The tool config JSON file schema is found in
     :ref:`config <config_tools_router_check_tool>`.
     The tool config input file specifies urls (composed of authorities and paths)
     and expected route parameter values. Additional parameters such as additional headers are optional.

Output
  The program exits with status EXIT_FAILURE if any test case does not match the expected route parameter
  value.

  The ``--details`` option prints out details for each test. The first line indicates the test name.

  If a test fails, details of the failed test cases are printed. The first field is the expected
  route parameter value. The second field is the actual route parameter value. The third field indicates
  the parameter that is compared. In the following example, Test_2 and Test_5 failed while the other tests
  passed. In the failed test cases, conflict details are printed. ::

    Test_1
    Test_2
    default other virtual_host_name
    Test_3
    Test_4
    Test_5
    locations ats cluster_name
    Test_6

  Testing with valid :ref:`runtime values <envoy_api_field_route.RouteMatch.runtime>` is not currently supported,
  this may be added in future work.

Building
  The tool can be built locally using Bazel. ::

    bazel build //test/tools/router_check:router_check_tool

Running
  The tool takes two input files and an optional command line parameter ``--details``. The
  expected order of command line arguments is:
  1. The router configuration file.
  2. The tool configuration json file.
  3. The optional details flag. ::

    bazel-bin/test/tools/router_check/router_check_tool router_config.(yaml|json) tool_config.json

    bazel-bin/test/tools/router_check/router_check_tool router_config.(yaml|json) tool_config.json --details

Testing
  A bash shell script test can be run with bazel. The test compares routes using different router and
  tool configuration files. The configuration files can be found in
  test/tools/router_check/test/config/... . ::

    bazel test //test/tools/router_check/...
