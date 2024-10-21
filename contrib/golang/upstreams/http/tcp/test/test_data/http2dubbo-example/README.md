# Documentation for http2dubbo by golang extension

* To simply support http2dubbo by go extension on envoy, we only need to write one extension by golang: **golang-tcp-upstream**.
* There are two funtion to achieve that.


## EncodeData
1. construct body from http to body

2. change dubbo method by envoy cluster_name or envoy router_name

3. set remote half close for conn

4. set envoy self not half close for conn


## OnUpstreamData

1. verify dubbo frame format

2. aggregate multi dubbo frame when server has big response

3. convert body from dubbo to http 