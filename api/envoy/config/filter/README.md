Protocol buffer definitions for filters.

Visibility of the definitions should be constrained to none except for
shared definitions between explicitly enumerated filters (e.g. accesslog and fault definitions).

## NOTE

If a filter configuration is not captured in the proto specification, you
can still supply plain JSON configuration objects for such filters by
setting the `"deprecated_v1"` field to true in the filter's
configuration. For example,

```json
{
 "name": "envoy.rate_limit",
  "config": {
    "deprecated_v1": true,
     "value": {
       "domain": "some_domain",
        "timeout_ms": 500
       }
    }
 }
```
