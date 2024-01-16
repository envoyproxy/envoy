# Command handlers

These simple diagrams show what are the main classes involved in providing Kafka-mesh filter functionality.

Disclaimer: these are not UML diagrams in any shape or form.

## Basics

Raw data is processed by `RequestDecoder`, which notifies `RequestProcessor` on successful parse.
`RequestProcessor` then creates `InFlightRequest` instances that can be processed.

When an `InFlightRequest` is finally processed, it can generate an answer (`AbstractResponse`)
that is later serialized by `ResponseEncoder`.

```mermaid
graph TD;
    InFlightRequest["<< abstract >> \n InFlightRequest"]
    AbstractResponse["<< abstract >> \n AbstractResponse"]

    KafkaMeshFilter <-.-> |"in-flight-reference\n(finish/abandon)"| InFlightRequest
    KafkaMeshFilter --> |feeds| RequestDecoder
    RequestDecoder --> |"notifies"| RequestProcessor
    RequestProcessor --> |"creates"| InFlightRequest
    InFlightRequest --> |"produces"| AbstractResponse

    RequestHolder["...RequestHolder"]
    RequestHolder --> |"subclass"| InFlightRequest

    KafkaMeshFilter --> ResponseEncoder
    ResponseEncoder -.-> |encodes| AbstractResponse
```

## Produce

Produce request (`ProduceRequestHolder`) uses `UpstreamKafkaFacade` to get `RichKafkaProducer` instances that
correspond to its topics.
When the deliveries have finished (successfully or not - the upstream could have rejected the records because
of its own reasons), `RichKafkaProducer` notifies the `ProduceRequestHolder` that it has finished.
The request can then notify its parent (`KafkaMeshFilter`) that the response can be sent downstream.

```mermaid
graph TD;
    KafkaMeshFilter <-.-> |"in-flight-reference\n(finish/abandon)"| ProduceRequestHolder
    KafkaMeshFilter --> RP["RequestDecoder+RequestProcessor"]
    RP --> |"creates"| ProduceRequestHolder
    UpstreamKafkaFacade --> |"accesses (Envoy thread-local)"| ThreadLocalKafkaFacade
    ThreadLocalKafkaFacade --> |"stores multiple"| RichKafkaProducer
    RdKafkaProducer["<< librdkafka >>\nRdKafkaProducer"]
    RichKafkaProducer --> |"wraps"| RdKafkaProducer
    RichKafkaProducer -.-> |"in-flight-reference\n(delivery callback)"| ProduceRequestHolder
    ProduceRequestHolder --> |uses| UpstreamKafkaFacade
    ProduceRequestHolder -.-> |sends data to| RichKafkaProducer
```

## Fetch

Fetch request (`FechRequestHolder`) registers itself with `SharedConsumerManager` to be notified when records matching
its interests appear.
`SharedConsumerManager` maintains multiple `RichKafkaConsumer` instances (what means keeps the Kafka consumer state)
that are responsible for polling records from upstream Kafka clusters.
Each `RichKafkaConsumer` is effectively a librdkafka `KafkaConsumer` and its poller thread.
When `FechRequestHolder` is finished with its processing (whether through record delivery or timeout), it uses an Envoy
`Dispatcher` to notify the parent filter.

```mermaid
graph TD;
    FRH["FechRequestHolder"]
    KafkaMeshFilter <-.-> |"in-flight-reference\n(finish/abandon)"| FRH
    KafkaMeshFilter --> RP["RequestDecoder+RequestProcessor"]
    RP --> |"creates"| FRH

    RCP["<< interface >> \n RecordCallbackProcessor"]
    SCM["SharedConsumerManager"]
    SCM --> |subclass| RCP

    KC["RichKafkaConsumer"]
    FRH -.-> |registers itself with| SCM
    SCM -.-> |provides records| FRH
    SCM --> |stores mutliple| KC

    LibrdKafkaConsumer["<< librdkafka >> \n KafkaConsumer"]
    ConsumerPoller["<< thread >> \n consumer poller"]
    KC --> |wraps| LibrdKafkaConsumer
    KC --> |holds| ConsumerPoller
    ConsumerPoller --> |polls from| LibrdKafkaConsumer

    DSP["<< Envoy >> \n Dispatcher"]
    KafkaMeshFilter ---  DSP
    FRH -.-> |notifies on finish| DSP
```
