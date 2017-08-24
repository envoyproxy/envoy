.. _config_http_filters_cors:

CORS filter
====================

This is a filter which handles CORS requests based on route or virtual host settings.
For the meaning of the headers please refer to the pages below.
https://developer.mozilla.org/en-US/docs/Web/HTTP/Access_control_CORS
https://www.w3.org/TR/cors/


.. code-block:: json

  {
    "type": "both",
    "name": "cors",
    "config": {}
  }


Settings
--------

Settings on a route take precedence over settings on the virtual host.
Both the route and the virtual host need to set enabled to true.

.. code-block:: json

  {
    "cors": {
        "enabled": false,
        "allow_origin": "",
        "allow_methods": "",
        "allow_headers": "",
        "allow_credentials": false,
        "expose_headers": "",
        "max_age": ""
    }
  }

enabled
  *(optional, boolean)* Defaults to false.

allow_origin
  *(required, string)* The content of the access-control-allow-origin header.

allow_methods
  *(optional, string)* The content of the access-control-allow-methods header.

allow_headers
  *(optional, string)* The content of the access-control-allow-headers header.

allow_credentials
  *(optional, boolean)* If the resource allows credentials.

expose_headers
  *(optional, string)* The content of the access-control-expose-headers header.

max_age
  *(optional, string)* The content of the access-control-max-age header.
