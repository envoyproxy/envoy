.. _install_tools_route_table_check_tool:

Route table check tool
=======================

The route table check tool checks whether the route parameters returned by a router match what is expected.
The tool can also be used to check whether a path redirect, path rewrite, or host rewrite
match what is expected.

Usage
  router_check_tool [-t <string>] [-c <string>] [-d] [-p] [--] [--version] [-h] <unlabelledConfigStrings>
    -t <string>,  --test-path <string>
      Path to a tool config JSON file. The tool config JSON file schema is found in
      :ref:`config <config_tools_router_check_tool>`.
      The tool config input file specifies urls (composed of authorities and paths)
      and expected route parameter values. Additional parameters such as additional headers are optional.

      Schema: All internal schemas in the tool are based on :repo:`proto3 <test/tools/router_check/validation.proto>`.

    -c <string>,  --config-path <string>
      Path to a router config file (YAML or JSON). The router config file schema is found in
      :ref:`config <envoy_v3_api_file_envoy/config/route/v3/route.proto>` and the config file extension
      must reflect its file type (for instance, .json for JSON and .yaml for YAML).

    -o <string>,  --output-path <string>
      Path to a file where to write test results as binary proto. If the file already exists,
      an attempt to overwrite it will be made. The validation result schema is found in
      :repo:`proto3 <test/tools/router_check/validation.proto>`.

    -d,  --details
      Show detailed test execution results. The first line indicates the test name.

    --only-show-failures
      Displays test results for failed tests. Omits test names for passing tests if the details flag is set.

    -f, --fail-under
      Represents a percent value for route test coverage under which the run should fail.

    --covall
      Enables comprehensive code coverage percent calculation taking into account all the possible
      asserts. Displays missing tests.

    --disable-deprecation-check
      Disables the deprecation check for RouteConfiguration proto.

    -h,  --help
      Displays usage information and exits.

Output
  The program exits with status EXIT_FAILURE if any test case does not match the expected route parameter
  value.

  If a test fails, details of the failed test cases are printed if ``--details`` flag is provided.
  The first field is the expected route parameter value. The second field is the actual route parameter value.
  The third field indicates the parameter that is compared.
  In the following example, Test_2 and Test_5 failed while the other tests
  passed. In the failed test cases, conflict details are printed. ::

    Test_1
    Test_2
    default other virtual_host_name
    Test_3
    Test_4
    Test_5
    locations ats cluster_name
    Test_6

  If an ``--output-path`` option is specified, then a ``ValidationResult`` proto message with the test results is written to a file.
  If the ``--only-show-failures`` flag is provided, only the failed test cases are written to a file.

Building
  The tool can be built locally using Bazel. ::

    bazel build //test/tools/router_check:router_check_tool

Running
  Example ::

    bazel-bin/test/tools/router_check/router_check_tool -c router_config.(yaml|json) -t tool_config.json --details

Testing
  A bash shell script test can be run with bazel. The test compares routes using different router and
  tool configuration files. The configuration files can be found in
  test/tools/router_check/test/config/... . ::

    bazel test //test/tools/router_check/...
