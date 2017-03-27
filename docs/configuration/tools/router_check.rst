.. _config_tools_router_check_tool:

Router check tool input interface
=================================

Input for the router check tool. The router check tool checks if the route returned
by a router matches what is expected. The expected field specifies the expected value
and is required. The authority and path fields specifies the url sent to the router
and are also required. The simplest configuration has one test case and takes the following form: ::

   {
     "input": [
       { "expected":"locations",
         "authority":"api.lyft.com",
         "path": "/api/locations"
       }
     ]
   }

.. code-block:: json

  {
    "input": {
      "type" : "array",
      "items" : {
        "type": "object",
        "properties": {
          "expected": { "type": "string" },
          "authority": { "type": "string" },
          "path": { "type": "string" },
          "additional_headers": {
            "type": "array",
            "items" : {
              "type": "object",
             "properties": {
                "name": { "type": "string" },
                "value": { "type": "string" }
               }
            }
          },
          "check": {
            "type": "object",
            "properties" : {
              "name": {"type" : "string", "enum" : ["cluster", "virtual_cluster", "virtual_host"] },
              "rewrite" : {"type" : "string", "enum" : ["host", "path"] },
              "redirect" : {"type" : "string", "enum" : ["path"] },
              "maxItems" : 1
            }
          },
          "method": { "type" : "string", "enum": ["GET", "PUT", "POST"] },
          "random_lb_value" : { "type" : "integer" },
          "ssl" : { "type" : "boolean" },
          "internal" : { "type" : "boolean" }
        },
        "additionalProperties": false,
        "required": ["expected", "authority", "path"]
      }
    }
  }

input
  *(required, array)* An array of expected and actual routes to match. For each element in the input array, the expected route and actual returned route are compared. The total number of matches and conflicts are output by the router check tool.

expected
  *(required, string)* A string containing the expected name or host or path of the route to be compared. Use the string "none" to specify no route is expected.

authority
  *(required, string)* The route authority. This value along with the path parameter define the url of the route to be matched. An example authority value is "api.lyft.com".

path
  *(required, string)* The route path. An example path value is "/foo".

additional_headers
  *(optional, array)*  Addtional headers to be added before a route is returned.

check
  *(optional, object)* The parameter to be matched. The default is to match the expected value with the returned cluster name. Only one of the following options can be chosen.

  name
    *(optional, string)* Choose from cluster, virtual_cluster and virtual_host. If cluster is selected, the returned anme of the clustr is matched with the string value of expected. If virtual_cluster is selected, the vitual cluster name is matched. If virtual_host is selected, the virtual host name is matched.

  rewrite
    *(optional, string)* Choose from host or path. If host is selected, the rewritten host is compared. If path is selected, the rewritten path is compared.

  redirect
    *(optional, string)* Compare the string value of expected to the returned redirect path.

method
  *(optional, string)* The request method. The default is GET in all cases but comparing redirect paths. In the redirect case, this parameter is not set in the request header.

random_lb_value
  *(optional, integer)* A random integer used when choosing between weighted load balanced clusters. The default value is 0.

ssl
  *(optional, boolean)* A flag that determines whether to set x-forwarded-proto to https or http. If not specified, this value is not set. In the redirect case, this value is default to false.

internal
  *(optional, boolean)* A flag that determines whether to set x-envoy-internal to true or false. If not specified, this value is not set. In the redirect case, this value is default to false.
