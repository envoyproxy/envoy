FROM envoyproxy/envoy:latest

RUN apt-get update && apt-get install -y curl tree
COPY enable_fault_injection.sh disable_fault_injection.sh send_request.sh /
