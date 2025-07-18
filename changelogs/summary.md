**Summary of changes**:

* TLS:
  - Fixed incorrectly cached connection properties on TLS connections that could cause network RBAC filters to fail.

* HTTP/2:
  - Fixed connection window buffer leak in oghttp2 that could cause connections to get stuck.

* Observability:
  - Fixed division by zero bug in Dynatrace sampling controller.

* Release:
  - Fixed permissions for distroless config directory.
  - Updated container images (Ubuntu/distroless).
