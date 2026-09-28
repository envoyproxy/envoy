Added ``unit_multiplier`` to the Rate Limit Service descriptor override and response APIs to
support rate limit periods that span multiple units of time. The HTTP rate limit filter uses the
response value when calculating the window in ``X-RateLimit-Limit`` headers.
