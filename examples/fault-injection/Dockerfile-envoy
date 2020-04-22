FROM envoyproxy/envoy-dev:latest

RUN apt-get update && apt-get install -y curl tree
COPY enable_delay_fault_injection.sh disable_delay_fault_injection.sh enable_abort_fault_injection.sh disable_abort_fault_injection.sh send_request.sh /
