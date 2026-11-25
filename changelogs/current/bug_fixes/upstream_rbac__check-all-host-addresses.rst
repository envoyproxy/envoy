Fixed the ``upstream_rbac`` HTTP filter evaluating RBAC policies only against the primary address of the
selected upstream host. The filter now evaluates the policies against all of the host's addresses, so a
request is denied when any address matches a ``DENY`` policy. Previously, a connection falling back to an
additional address (e.g. via Happy Eyeballs) could reach a denied address without it being checked.
