Envoy fault injection example
=============================

This simple example demonstrates [Envoy's fault injection
capability](https://www.envoyproxy.io/docs/envoy/latest/configuration/http_filters/fault_filter) using [Envoy's runtime
support](https://www.envoyproxy.io/docs/envoy/latest/configuration/runtime) to control the feature.

## Usage
```
# in terminal 1, launch Envoy and backend service containers.
docker-compose pull
docker-compose up

# in terminal 2
docker-compose exec envoy bash
bash send_request.sh
```

The script above (`send_request.sh`) sends a continuous stream of HTTP requests to Envoy, which in turn forwards the
requests to the backend container. Fault injection is configured in Envoy but turned off (i.e. affects 0% of requests).
Consequently, you should see a continuous sequence of HTTP 200 response code.


### Abort fault injection
Turn on _abort_ fault injection via the runtime using the commands below.

```
# in terminal 3
docker-compose exec envoy bash
bash enable_abort_fault_injection.sh
```

The script above enables HTTP aborts for 100% of requests. So, you should now see a continuous sequence of HTTP 503
responses for all requests.

To disable the abort injection:

```
# in terminal 3 in the "envoy" container
bash disable_abort_fault_injection.sh
```

### Delay fault injection
Turn on _delay_ fault injection via the runtime using the commands below.

```
# in terminal 3
docker-compose exec envoy bash
bash enable_delay_fault_injection.sh
```

The script above will add a 3-second delay to 50% of HTTP requests. You should now see a continuous sequence of HTTP 200 responses for all requests, but half of the requests will take 3 seconds to complete.

To disable the delay injection:

```
# in terminal 3 in the "envoy" container
bash disable_delay_fault_injection.sh
```

### The current runtime
To see the current runtime filesystem overview:

```
# in terminal 3 in the "envoy" container
tree /srv/runtime
```
