#!/bin/bash -e

export NAME=kafka-broker

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Waiting for Kafka on port 9092."
wait_for 120 sh -c "docker-compose logs -f kafka | grep -q 'started (kafka.server.KafkaServer)'"
run_log "Kafka launched."

# Topic
TOPIC="envoy.kafka.broker"
run_log "Creating topic."
docker-compose exec kafka kafka-topics --bootstrap-server localhost:19092 --create --topic $TOPIC quickstart-events

run_log "Checking topic."
topic_created=$(docker-compose exec kafka kafka-topics --bootstrap-server localhost:19092 --list)
if [[ "$topic_created" == "$TOPIC" ]]; then
    run_log "Checked topic $topic_created succesfully."
else
    run_log "Checked topic $topic_created failed."
    exit 1
fi

# Initialize message for producer
MESSAGE="Welcome to Envoy and Kafka Broker filter!"

# Producer
run_log "Create Producer and send message."
docker-compose exec kafka /bin/bash -c "echo $MESSAGE >> message.txt & kafka-console-producer --request-required-acks 1 --broker-list localhost:19092 --topic envoy.kafka.broker < message.txt"
run_log "Sent messages succesfully."

# Consumer
run_log "Create Consumer."
# `--timeout-ms 10000` lets the consumer exit after receiving the message. And
# ```
# ERROR Error processing message, terminating consumer process:  (kafka.tools.ConsoleConsumer$)
# org.apache.kafka.common.errors.TimeoutException
# Processed a total of 1 messages
# ```
# is the expected error
message_received=$(docker-compose exec kafka kafka-console-consumer --bootstrap-server localhost:19092 --topic $TOPIC --from-beginning --timeout-ms 10000)
if [[ "$message_received" == "$MESSAGE" ]]; then
    run_log "Received message succesfully."
else
    run_log "Received message failed."
fi
