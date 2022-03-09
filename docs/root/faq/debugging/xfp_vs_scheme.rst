.. _why_is_envoy_using_xfp_or_scheme:

Why is Envoy operating on X-Forwarded-Proto instead of :scheme or vice-versa?
=============================================================================

With almost all requests, the value of the X-Forwarded-Proto header and the :scheme
header (if present) will be the same. Generally users request https:// resources over
TLS connections and http:// resources in the clear. However, it is entirely possible
for a user to request http:// content over a TLS connection or in internal meshes to forward
https:// requests in cleartext. In these cases Envoy will attempt to use the :scheme
header when referring to content (say serving a given entity out of cache based on the URL
scheme) and the X-Forwarded-Proto header when doing operations related to underlying
encryption (stripping the default port based on if the request was TLS on port 443, or
cleartext on port 80)
