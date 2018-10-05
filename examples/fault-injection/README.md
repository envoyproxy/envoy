Envoy fault injection example
=============================

In this example, we show [Envoy's fault injection
feature](https://www.envoyproxy.io/docs/envoy/latest/configuration/http_filters/fault_filter) in a sandbox.

## Usage
```
# in terminal 1
docker-compose up

# in terminal 2
docker-compose exec envoy bash
bash send_request.sh
```

This constantly sends HTTP GET requests to running Envoy process. The process proxies the requests to the backend server
which is running python process in the "backend" container. We see HTTP 200 responses here.

Then let's inject a fault using [Envoy's runtime
feature](https://www.envoyproxy.io/docs/envoy/latest/configuration/runtime).

```
# in terminal 3
docker-compose exec envoy bash
bash enable_fault_jnjection.sh
```

This changes Envoy's fault injection filter config via runtime setting. Then `send_request.sh` script shows HTTP 503
responses.

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
