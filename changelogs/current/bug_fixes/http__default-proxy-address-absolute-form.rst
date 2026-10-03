Fixed a bug where Envoy configured with ``default_proxy_address`` for cleartext HTTP/1 traffic could
connect to the proxy but still encode the request target in origin-form instead of the absolute-form
required for a forward proxy. Requests now correctly use the absolute URL form, such as
``GET http://example.com/path HTTP/1.1``, when the proxy is selected through the default proxy
address.
