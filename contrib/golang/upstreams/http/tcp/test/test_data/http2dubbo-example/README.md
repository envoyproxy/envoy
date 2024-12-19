# Documentation for http2dubbo by golang extension

* To simply support http2dubbo by go extension on envoy, we only need to write one extension by golang: **golang-tcp-upstream**.

## Example 

### Build golang-so with filter.go and config.go
> cd ./ \
> go mod tidy \
> go build -v -o ./envoy/libgolang.so -buildmode=c-shared . 


### Run Envoy with golang-so
> run envoy with ./envoy.yaml 
>> (remember to replace golang-so path with your own).

### Run Dubbo Server
> cd ./java-dubbo-server \
> refer to README.md in the corresponding dir \
> run dubbo server


#### Here is the core code for dubbo server:

> dubbo Interface: com.alibaba.nacos.example.dubbo.service.DemoService \
> dubbo method: sayName
    
    public class DefaultService implements DemoService {
        public String sayName(String name) {
            return String.format("Hello, %s !\n", name);
        }
    }

if we pass <u>name</u> param in dubbo req body, it will return <u>Hello {name} !</u>.

### Use Curl to req envoy

#### demo-1
> First demo is a request without any body and header.

    curl 127.0.0.1:10001/mytest.service/sayHello -X GET -v

    * Request Msg:
    > POST /mytest.service/sayHello HTTP/1.1
    > Host: 127.0.0.1:10001
    > User-Agent: curl/7.68.0
    > Accept: */*
    
    * Response Msg: 
    < HTTP/1.1 200 OK
    < content-type: application/json; charset=utf-8
    < extension: golang-tcp-upstream
    < x-envoy-upstream-service-time: 15
    < date: Thu, 31 Oct 2024 09:32:08 GMT
    < server: envoy
    < transfer-encoding: chunked
    <
    Hello, mock !


#### demo-2
> Second demo is a request with body (contain <u>name</u> param for dubbo) and header (container dubbo interface and method). \
> when you req with body which contains key: <u>name</u>, value: <u>jack</u>, the response will be: <u>Hello jack ! </u>.

    curl 127.0.0.1:10001/mytest.service/sayHello -X POST -d '{"name": "jack"}' -H 'dubbo_method: sayName' -H 'dubbo_interface: com.alibaba.nacos.example.dubbo.service.DemoService' -v

    * Request Msg:
    > POST /mytest.service/sayHello HTTP/1.1
    > Host: 127.0.0.1:10001
    > User-Agent: curl/7.68.0
    > Accept: */*
    > dubbo_method: sayName
    > dubbo_interface: com.alibaba.nacos.example.dubbo.service.DemoService
    > Content-Length: 16
    > Content-Type: application/x-www-form-urlencoded
    
    * Response Msg: 
    < HTTP/1.1 200 OK
    < content-type: application/json; charset=utf-8
    < extension: golang-tcp-upstream
    < x-envoy-upstream-service-time: 4
    < date: Thu, 31 Oct 2024 09:32:12 GMT
    < server: envoy
    < transfer-encoding: chunked
    <
    Hello, jack !






## Custom Introduction: How to achieve golang tcp upstream extension ?
* You need to achieve four golang funtion to support http2tcp in envoy.

#### EncodeHeaders Usages
  - get dubboMethod, dubboInterface from headers

  - construct & set data for sending to upstream

#### EncodeData Usages
  - append data when streaming get data from downstream

  - get dubboArgs from http body
  - change dubboInterface for gray_traffic by router_name
  - construct & set data for sending to upstream
  - set remote half close for conn by cluster_name
  - set envoy-self half close for conn

#### OnUpstreamData Usages
  - verify dubbo frame format

  - aggregate multi dubbo frame when server has big response
  - convert body from dubbo to http
  - construct http response header

#### OnDestroy Usages
  - do something when destroy request