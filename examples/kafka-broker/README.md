To learn about this configuration please head over
to the [Envoy docs](https://www.envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/kafka_broker_filter).

### Start the services, Create Topic & Producer
There are three parts of this script:
- Start the services by docker-compose
- Create a topic
- Create a producer and send messages via typing
```console
$ ./verify.sh
```

### Create Consumer
Open a new terminal window and create the consumer to receive messages
```console
$ export TOPIC="envoy.kafka.broker"
$ echo "> [Kafka-broker] Consumer is receiving messages..."
$ docker exec kafka kafka-console-consumer --bootstrap-server localhost:19092 --topic $TOPIC --from-beginning
```