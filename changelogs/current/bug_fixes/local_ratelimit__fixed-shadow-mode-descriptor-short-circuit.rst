Fixed a bug in local rate limiter where an exhausted ``shadow_mode: true`` descriptor would
short-circuit descriptor evaluation and prevent subsequent enforced descriptors or the default
token bucket from being consumed.
