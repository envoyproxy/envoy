Fixed the Go HTTP filter ignoring ``strict`` in ``SetUpstreamOverrideHost`` when called from a
goroutine. The override host was always applied in non-strict mode in that case, so an unavailable
host fell back to another endpoint instead of failing the request.
