.. _config_http_filters_cors:

CORS filter
====================

This is a filter which handles Cross-Origin Resource Sharing requests based on route or virtual host settings.
For the meaning of the headers please refer to the pages below.

- https://developer.mozilla.org/en-US/docs/Web/HTTP/Access_control_CORS
- https://www.w3.org/TR/cors/


.. code-block:: json

  {
    "name": "cors",
    "config": {}
  }


Settings
--------

Settings on a route take precedence over settings on the virtual host.

.. code-block:: json

  {
    "cors": {
        "enabled": false,
        "allow_origin": ["http://foo.example"],
        "allow_methods": "POST, GET, OPTIONS",
        "allow_headers": "Content-Type",
        "allow_credentials": false,
        "expose_headers": "X-Custom-Header",
        "max_age": "86400"
    }
  }

enabled
  *(optional, boolean)* Defaults to true. Setting *enabled* to false on a route disables CORS
  for this route only. The setting has no effect on a virtual host.

allow_origin
  *(optional, array)* The origins that will be allowed to do CORS request.
  Wildcard "\*" will allow any origin.

allow_methods
  *(optional, string)* The content for the *access-control-allow-methods* header.
  Comma separated list of HTTP methods.

allow_headers
  *(optional, string)* The content for the *access-control-allow-headers* header.
  Comma separated list of HTTP headers.

allow_credentials
  *(optional, boolean)* Whether the resource allows credentials.

expose_headers
  *(optional, string)* The content for the *access-control-expose-headers* header.
  Comma separated list of HTTP headers.

max_age
  *(optional, string)* The content for the *access-control-max-age* header.
  Value in seconds for how long the response to the preflight request can be cached.
