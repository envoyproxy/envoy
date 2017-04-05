.. _config_tools_router_check_tool:

Route table check tool
======================

**NOTE: The following configuration is for the router table check tool only and is not part of the envoy binary.
The route table check tool is used for testing purposes only.**

Input for the route table check tool. The route table check tool checks if the route returned
by a router matches what is expected. The tool can be used to check cluster name, virtual cluster name,
virtual host name, path rewrite, host rewrite, and path redirect matches. Extensions for other
test cases can be added. The "check" field specifies the expected values in each test case. At least one test
case is required. In addition, the authority and path fields specify the url sent to the router
and are also required. A simple configuration has one test case and is writen as follows. The test
expects a cluster name match of "instant-server".::

   [
     {
       "authority":"api.lyft.com",
       "path": "/api/locations",
       "check": {"cluster_name": "instant-server"}
     }
   ]

.. code-block:: json

  [
    {
      "authority": "...",
      "path": "...",
      "additional_headers": [
        {
          "name": "...",
          "value": "..."
        },
        {
          "..."
        }
      ],
      "method": "...",
      "random_lb_value" : "...",
      "ssl" : "...",
      "internal" : "...",
      "check": {
        "cluster_name": "...",
        "virtual_cluster_name": "...",
        "virtual_host_name": "...",
        "path_rewrite": "...",
        "host_rewrite": "...",
        "path_redirect": "..."
      }
    },
    {
      "..."
    }
  ]

authority
  *(required, string)* The url authority. This value along with the path parameter define
  the url to be matched. An example authority value is "api.lyft.com".

path
  *(required, string)* The url path. An example path value is "/foo".

additional_headers
  *(optional, array)*  Additional headers to be added before a route is returned.

method
  *(optional, string)* The request method. If not specified, the default method is GET in all test cases
  except for the redirect path test case. In the redirect path case, this parameter is not set by default.

random_lb_value
  *(optional, integer)* A random integer used when choosing between weighted load balanced clusters.
  The default value is 0.

ssl
  *(optional, boolean)* A flag that determines whether to set x-forwarded-proto to https or http.
  In the redirect path test case, this value is set to false by default. In all other test cases,
  this value is not set by default.

internal
  *(optional, boolean)* A flag that determines whether to set x-envoy-internal to "true".
  If not specified, or if internal is equal to false, x-envoy-internal is not set.

check
  *(required, object)* The check object specifies the returned router parameters to match. At least one
  test parameter must be specificed.

  cluster_name
    *(optional, string)* Match the cluster name.

  virutal_cluster_name
    *(optional, string)* Match the virtual cluster name.

  virtual_host_name
    *(optional, string)* Match the virtual host name.

  path_rewrite
    *(optional, string)* Match the path header field after rewrite.

  host_rewrite
    *(optional, string)* Match the host header field after rewrite.

  path_redirect
    *(optional, string)* Match the returned redirect path.
