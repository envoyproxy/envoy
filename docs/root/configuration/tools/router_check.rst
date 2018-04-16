.. _config_tools_router_check_tool:

Route table check tool
======================

**NOTE: The following configuration is for the route table check tool only and is not part of the Envoy binary.
The route table check tool is a standalone binary that can be used to verify Envoy's routing for a given configuration
file.**

The following specifies input to the route table check tool. The route table check tool checks if
the route returned by a :ref:`router <config_http_conn_man_route_table>` matches what is expected.
The tool can be used to check cluster name, virtual cluster name,
virtual host name, manual path rewrite, manual host rewrite, path redirect, and
header field matches. Extensions for other test cases can be added. Details about installing the tool
and sample tool input/output can be found at :ref:`installation <install_tools_route_table_check_tool>`.

The route table check tool config is composed of an array of json test objects. Each test object is composed of
three parts.

Test name
  This field specifies the name of each test object.

Input values
  The input value fields specify the parameters to be passed to the router. Example input fields include
  the :authority, :path, and :method header fields. The :authority and :path fields specify the url
  sent to the router and are required. All other input fields are optional.

Validate
  The validate fields specify the expected values and test cases to check. At least one test
  case is required.

A simple tool configuration json has one test case and is written as follows. The test
expects a cluster name match of "instant-server".::

   [
     {
       "test_name: "Cluster_name_test",
       "input":
         {
           ":authority":"api.lyft.com",
           ":path": "/api/locations"
         }
       "validate"
         {
           "cluster_name": "instant-server"
         }
      }
   ]

.. code-block:: json

  [
    {
      "test_name": "...",
      "input":
        {
          ":authority": "...",
          ":path": "...",
          ":method": "...",
          "internal" : "...",
          "random_value" : "...",
          "ssl" : "...",
          "additional_headers": [
            {
              "field": "...",
              "value": "..."
            },
            {
               "..."
            }
          ]
        }
      "validate": {
        "cluster_name": "...",
        "virtual_cluster_name": "...",
        "virtual_host_name": "...",
        "host_rewrite": "...",
        "path_rewrite": "...",
        "path_redirect": "...",
        "header_fields" : [
          {
            "field": "...",
            "value": "..."
          },
          {
            "..."
          }
        ]
      }
    },
    {
      "..."
    }
  ]

test_name
  *(required, string)* The name of a test object.

input
  *(required, object)* Input values sent to the router that determine the returned route.

  :authority
    *(required, string)* The url authority. This value along with the path parameter define
    the url to be matched. An example authority value is "api.lyft.com".

  :path
    *(required, string)* The url path. An example path value is "/foo".

  :method
    *(optional, string)* The request method. If not specified, the default method is GET. The options
    are GET, PUT, or POST.

  internal
    *(optional, boolean)* A flag that determines whether to set x-envoy-internal to "true".
    If not specified, or if internal is equal to false, x-envoy-internal is not set.

  random_value
    *(optional, integer)* An integer used to identify the target for weighted cluster selection.
    The default value of random_value is 0.

  ssl
    *(optional, boolean)* A flag that determines whether to set x-forwarded-proto to https or http.
    By setting x-forwarded-proto to a given protocol, the tool is able to simulate the behavior of
    a client issuing a request via http or https. By default ssl is false which corresponds to
    x-forwarded-proto set to http.

  additional_headers
    *(optional, array)*  Additional headers to be added as input for route determination. The ":authority",
    ":path", ":method", "x-forwarded-proto", and "x-envoy-internal" fields are specified by the other config
    options and should not be set here.

    field
      *(required, string)* The name of the header field to add.

    value
      *(required, string)* The value of the header field to add.

validate
  *(required, object)* The validate object specifies the returned route parameters to match. At least one
  test parameter must be specificed. Use "" (empty string) to indicate that no return value is expected.
  For example, to test that no cluster match is expected use {"cluster_name": ""}.

  cluster_name
    *(optional, string)* Match the cluster name.

  virtual_cluster_name
    *(optional, string)* Match the virtual cluster name.

  virtual_host_name
    *(optional, string)* Match the virtual host name.

  host_rewrite
    *(optional, string)* Match the host header field after rewrite.

  path_rewrite
    *(optional, string)* Match the path header field after rewrite.

  path_redirect
    *(optional, string)* Match the returned redirect path.

  header_fields
    *(optional, array)*  Match the listed header fields. Examples header fields include the ":path", "cookie",
    and "date" fields. The header fields are checked after all other test cases. Thus, the header fields checked
    will be those of the redirected or rewriten routes when applicable.

    field
      *(required, string)* The name of the header field to match.

    value
      *(required, string)* The value of the header field to match.
