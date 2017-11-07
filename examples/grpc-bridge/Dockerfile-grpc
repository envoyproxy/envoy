FROM envoyproxy/envoy:latest

RUN mkdir /var/log/envoy/
COPY ./bin/service /usr/local/bin/srv
COPY ./script/grpc_start /etc/grpc_start
CMD /etc/grpc_start

