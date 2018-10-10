Envoy fault injection example
=============================

This simple example demonstrates [Envoy's fault injection
capability](https://www.envoyproxy.io/docs/envoy/latest/configuration/http_filters/fault_filter) using [Envoy's runtime
support](https://www.envoyproxy.io/docs/envoy/latest/configuration/runtime) to control the feature.


## Usage
```
# in terminal 1, launch Envoy and backend service containers.
docker-compose up

# in terminal 2
docker-compose exec envoy bash
bash send_request.sh
```

The script above (`send_request.sh`) sends a continuous stream of HTTP requests to Envoy, which in turn forwards the
requests to the backend container. Fault injection is configured in Envoy but turned off (i.e. affects 0% of requests).
Consequently, you should see a continuous sequence of HTTP 200 response code.

Tun on fault injection via the runtime using the commands below.

```
# in terminal 3
docker-compose exec envoy bash
bash enable_fault_injection.sh
```

The script above enables HTTP aborts for 100% of requests. So, you should now see a continuous sequence of HTTP 503
responses for all requests.

To disable the fault injection:

```
# in terminal 3 in the "envoy" container
bash disable_fault_injection.sh
```

To see current runtime filesystem overview:

```
# in terminal 3 in the "envoy" container
tree /srv/runtime
```
