#!/bin/bash -e

export NAME=kafka-broker

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Waiting for Zookeeper and Kafka started."
sleep 40

TOPIC="envoy.kafka.broker"
run_log "Creating topic."
docker exec kafka kafka-topics --bootstrap-server localhost:19092 --create --topic $TOPIC quickstart-events
sleep 5

run_log "Producer is sending messages. Please type..."
docker exec -it kafka kafka-console-producer --request-required-acks 1 --broker-list localhost:19092 --topic $TOPIC
