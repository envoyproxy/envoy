#!/usr/bin/python

import random
import os
import shutil
import socket
import subprocess
import tempfile
from threading import Thread, Semaphore
import time
import unittest
import random

from kafka import KafkaConsumer, KafkaProducer, TopicPartition
import urllib.request


class Message:
    """
    Stores data sent to Envoy / Kafka.
    """

    def __init__(self):
        self.key = os.urandom(256)
        self.value = os.urandom(2048)
        self.headers = [('header_' + str(h), os.urandom(128)) for h in range(3)]


class IntegrationTest(unittest.TestCase):
    """
    All tests in this class depend on Envoy/Zookeeper/Kafka running.
    For each of these tests we are going to create Kafka producers and consumers, with producers
    pointing to Envoy (so the records get forwarded to target Kafka clusters) and verifying consumers
    pointing to Kafka clusters directly (as mesh filter does not yet support Fetch requests).
    We expect every operation to succeed (as they should reach Kafka) and the corresponding metrics
    to increase on Envoy side (to show that messages were received and forwarded successfully).
    """

    services = None

    @classmethod
    def setUpClass(cls):
        IntegrationTest.services = ServicesHolder()
        IntegrationTest.services.start()

    @classmethod
    def tearDownClass(cls):
        IntegrationTest.services.shut_down()

    def setUp(self):
        # We want to check if our services are okay before running any kind of test.
        IntegrationTest.services.check_state()
        self.metrics = MetricsHolder(self)

    def tearDown(self):
        # We want to check if our services are okay after running any test.
        IntegrationTest.services.check_state()

    @classmethod
    def kafka_envoy_address(cls):
        return '127.0.0.1:%s' % IntegrationTest.services.kafka_envoy_port

    @classmethod
    def kafka_cluster1_address(cls):
        return '127.0.0.1:%s' % IntegrationTest.services.kafka_real_port1

    @classmethod
    def kafka_cluster2_address(cls):
        return '127.0.0.1:%s' % IntegrationTest.services.kafka_real_port2

    @classmethod
    def envoy_stats_address(cls):
        return 'http://127.0.0.1:%s/stats' % IntegrationTest.services.envoy_monitoring_port

    def test_producing(self):
        """
        This test verifies that producer can send messages through mesh filter.
        We are going to send messages to two topics: 'apples' and 'bananas'.
        The mesh filter is configured to forward records for topics starting with 'a' (like 'apples')
        to the first cluster, and the ones starting with 'b' (so 'bananas') to the second one.

        We are going to send messages one by one, so they will not be batched in Kafka producer,
        so the filter is going to receive them one by one too.

        After sending, the consumers are going to read from Kafka clusters directly to make sure that
        nothing was lost.
        """

        messages_to_send = 100
        partition1 = TopicPartition('apples', 0)
        partition2 = TopicPartition('bananas', 0)

        producer = KafkaProducer(
            bootstrap_servers=IntegrationTest.kafka_envoy_address(), api_version=(1, 0, 0))
        offset_to_message1 = {}
        offset_to_message2 = {}
        for _ in range(messages_to_send):
            message = Message()
            future1 = producer.send(
                key=message.key,
                value=message.value,
                headers=message.headers,
                topic=partition1.topic,
                partition=partition1.partition)
            self.assertTrue(future1.get().offset >= 0)
            offset_to_message1[future1.get().offset] = message

            future2 = producer.send(
                key=message.key,
                value=message.value,
                headers=message.headers,
                topic=partition2.topic,
                partition=partition2.partition)
            self.assertTrue(future2.get().offset >= 0)
            offset_to_message2[future2.get().offset] = message
        self.assertTrue(len(offset_to_message1) == messages_to_send)
        self.assertTrue(len(offset_to_message2) == messages_to_send)
        producer.close()

        # Check the target clusters.
        self.__verify_target_kafka_cluster(
            IntegrationTest.kafka_cluster1_address(), partition1, offset_to_message1, partition2)
        self.__verify_target_kafka_cluster(
            IntegrationTest.kafka_cluster2_address(), partition2, offset_to_message2, partition1)

        # Check if requests have been received.
        self.metrics.collect_final_metrics()
        self.metrics.assert_metric_increase('produce', 200)

    def test_producing_with_batched_records(self):
        """
        Compared to previous test, we are going to have batching in Kafka producers (this is caused by high 'linger.ms' value).
        So a single request that reaches a Kafka broker might be carrying more than one record, for different partitions.
        """
        messages_to_send = 100
        partition1 = TopicPartition('apricots', 0)
        partition2 = TopicPartition('berries', 0)

        # This ensures that records to 'apricots' and 'berries' partitions.
        producer = KafkaProducer(
            bootstrap_servers=IntegrationTest.kafka_envoy_address(),
            api_version=(1, 0, 0),
            linger_ms=1000,
            batch_size=100)
        future_to_message1 = {}
        future_to_message2 = {}
        for _ in range(messages_to_send):
            message = Message()
            future1 = producer.send(
                key=message.key,
                value=message.value,
                headers=message.headers,
                topic=partition1.topic,
                partition=partition1.partition)
            future_to_message1[future1] = message

            message = Message()
            future2 = producer.send(
                key=message.key,
                value=message.value,
                headers=message.headers,
                topic=partition2.topic,
                partition=partition2.partition)
            future_to_message2[future2] = message

        offset_to_message1 = {}
        offset_to_message2 = {}
        for future in future_to_message1.keys():
            offset_to_message1[future.get().offset] = future_to_message1[future]
            self.assertTrue(future.get().offset >= 0)
        for future in future_to_message2.keys():
            offset_to_message2[future.get().offset] = future_to_message2[future]
            self.assertTrue(future.get().offset >= 0)
        self.assertTrue(len(offset_to_message1) == messages_to_send)
        self.assertTrue(len(offset_to_message2) == messages_to_send)
        producer.close()

        # Check the target clusters.
        self.__verify_target_kafka_cluster(
            IntegrationTest.kafka_cluster1_address(), partition1, offset_to_message1, partition2)
        self.__verify_target_kafka_cluster(
            IntegrationTest.kafka_cluster2_address(), partition2, offset_to_message2, partition1)

        # Check if requests have been received.
        self.metrics.collect_final_metrics()
        self.metrics.assert_metric_increase('produce', 1)

    def __verify_target_kafka_cluster(
            self, bootstrap_servers, partition, offset_to_message_map, other_partition):
        # Check if records were properly forwarded to the cluster.
        consumer = KafkaConsumer(bootstrap_servers=bootstrap_servers, auto_offset_reset='earliest')
        consumer.assign([partition])
        received_messages = []
        while (len(received_messages) < len(offset_to_message_map)):
            poll_result = consumer.poll(timeout_ms=1000)
            received_messages += poll_result[partition]
        self.assertTrue(len(received_messages) == len(offset_to_message_map))
        for record in received_messages:
            sent_message = offset_to_message_map[record.offset]
            self.assertTrue(record.key == sent_message.key)
            self.assertTrue(record.value == sent_message.value)
            self.assertTrue(record.headers == sent_message.headers)

        # Check that no records were incorrectly routed from the "other" partition (they would have created the topics).
        self.assertTrue(other_partition.topic not in consumer.topics())
        consumer.close(False)

    def test_consumer_stateful_proxy(self):
        """
        This test verifies that consumer can receive messages through the mesh filter.
        We are going to have messages in two topics: 'aaaconsumer' and 'bbbconsumer'.
        The mesh filter is configured to process fetch requests for topics starting with 'a' (like 'aaaconsumer')
        by consuming from the first cluster, and the ones starting with 'b' (so 'bbbconsumer') from the second one.
        So in the end our consumers that point at Envoy should receive records from matching upstream Kafka clusters.
        """

        # Put the messages into upstream Kafka clusters.
        partition1 = TopicPartition('aaaconsumer', 0)
        count1 = 20
        partition2 = TopicPartition('bbbconsumer', 0)
        count2 = 30
        self.__put_messages_into_upstream_kafka(
            IntegrationTest.kafka_cluster1_address(), partition1, count1)
        self.__put_messages_into_upstream_kafka(
            IntegrationTest.kafka_cluster2_address(), partition2, count2)

        # Create Kafka consumers that point at Envoy.
        consumer1 = KafkaConsumer(bootstrap_servers=IntegrationTest.kafka_envoy_address())
        consumer1.assign([partition1])
        consumer2 = KafkaConsumer(bootstrap_servers=IntegrationTest.kafka_envoy_address())
        consumer2.assign([partition2])

        # Have the consumers receive the messages from Kafka clusters through Envoy.
        received1 = []
        received2 = []
        while (len(received1) < count1):
            poll_result = consumer1.poll(timeout_ms=5000)
            for records in poll_result.values():
                received1 += records
        while (len(received2) < count2):
            poll_result = consumer2.poll(timeout_ms=5000)
            for records in poll_result.values():
                received2 += records

        # Verify that the messages sent have been received.
        self.assertTrue(len(received1) == count1)
        self.assertTrue(len(received2) == count2)

        # Cleanup
        consumer1.close(False)
        consumer2.close(False)

    def __put_messages_into_upstream_kafka(self, bootstrap_servers, partition, count):
        """
        Helper method for putting messages into Kafka directly.
        """
        producer = KafkaProducer(bootstrap_servers=bootstrap_servers)

        futures = []
        for _ in range(count):
            message = Message()
            future = producer.send(
                key=message.key,
                value=message.value,
                headers=message.headers,
                topic=partition.topic,
                partition=partition.partition)
            futures.append(future)
        for future in futures:
            offset = future.get().offset
            print('Saved message at offset %s' % (offset))
        producer.close(True)


class MetricsHolder:
    """
    Utility for storing Envoy metrics.
    Expected to be created before the test (to get initial metrics), and then to collect them at the
    end of test, so the expected increases can be verified.
    """

    def __init__(self, owner):
        self.owner = owner
        self.initial_requests, self.inital_responses = MetricsHolder.get_envoy_stats()
        self.final_requests = None
        self.final_responses = None

    def collect_final_metrics(self):
        self.final_requests, self.final_responses = MetricsHolder.get_envoy_stats()

    def assert_metric_increase(self, message_type, count):
        request_type = message_type + '_request'
        response_type = message_type + '_response'

        initial_request_value = self.initial_requests.get(request_type, 0)
        final_request_value = self.final_requests.get(request_type, 0)
        self.owner.assertGreaterEqual(final_request_value, initial_request_value + count)

        initial_response_value = self.inital_responses.get(response_type, 0)
        final_response_value = self.final_responses.get(response_type, 0)
        self.owner.assertGreaterEqual(final_response_value, initial_response_value + count)

    @staticmethod
    def get_envoy_stats():
        """
        Grab request/response metrics from envoy's stats interface.
        """

        stats_url = IntegrationTest.envoy_stats_address()
        requests = {}
        responses = {}
        with urllib.request.urlopen(stats_url) as remote_metrics_url:
            payload = remote_metrics_url.read().decode()
            lines = payload.splitlines()
            for line in lines:
                request_prefix = 'kafka.testfilter.request.'
                response_prefix = 'kafka.testfilter.response.'
                if line.startswith(request_prefix):
                    data = line[len(request_prefix):].split(': ')
                    requests[data[0]] = int(data[1])
                    pass
                if line.startswith(response_prefix) and '_response:' in line:
                    data = line[len(response_prefix):].split(': ')
                    responses[data[0]] = int(data[1])
        return [requests, responses]


class ServicesHolder:
    """
    Utility class for setting up our external dependencies: Envoy, Zookeeper
    and two Kafka clusters (single-broker each).
    """

    def __init__(self):
        self.kafka_tmp_dir = None

        self.envoy_worker = None
        self.zk_worker = None
        self.kafka_workers = None

    @staticmethod
    def get_random_listener_port():
        """
    Here we count on OS to give us some random socket.
    Obviously this method will need to be invoked in a try loop anyways, as in degenerate scenario
    someone else might have bound to it after we had closed the socket and before the service
    that's supposed to use it binds to it.
    """

        import socket
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
            server_socket.bind(('0.0.0.0', 0))
            socket_port = server_socket.getsockname()[1]
            print('returning %s' % socket_port)
            return socket_port

    def start(self):
        """
        Starts all the services we need for integration tests.
        """

        # Find java installation that we are going to use to start Zookeeper & Kafka.
        java_directory = ServicesHolder.find_java()

        launcher_environment = os.environ.copy()
        # Make `java` visible to build script:
        # https://github.com/apache/kafka/blob/2.2.0/bin/kafka-run-class.sh#L226
        new_path = os.path.abspath(java_directory) + os.pathsep + launcher_environment['PATH']
        launcher_environment['PATH'] = new_path
        # Both ZK & Kafka use Kafka launcher script.
        # By default it sets up JMX options:
        # https://github.com/apache/kafka/blob/2.2.0/bin/kafka-run-class.sh#L167
        # But that forces the JVM to load file that is not present due to:
        # https://docs.oracle.com/javase/9/management/monitoring-and-management-using-jmx-technology.htm
        # Let's make it simple and just disable JMX.
        launcher_environment['KAFKA_JMX_OPTS'] = ' '

        # Setup a temporary directory, which will be used by Kafka & Zookeeper servers.
        self.kafka_tmp_dir = tempfile.mkdtemp()
        print('Temporary directory used for tests: ' + self.kafka_tmp_dir)

        # This directory will store the configuration files fed to services.
        config_dir = self.kafka_tmp_dir + '/config'
        os.mkdir(config_dir)
        # This directory will store Zookeeper's data (== Kafka server metadata).
        zookeeper_store_dir = self.kafka_tmp_dir + '/zookeeper_data'
        os.mkdir(zookeeper_store_dir)
        # These directories will store Kafka's data (== partitions).
        kafka_store_dir1 = self.kafka_tmp_dir + '/kafka_data1'
        os.mkdir(kafka_store_dir1)
        kafka_store_dir2 = self.kafka_tmp_dir + '/kafka_data2'
        os.mkdir(kafka_store_dir2)

        # Find the Kafka server 'bin' directory.
        kafka_bin_dir = os.path.join('.', 'external', 'kafka_server_binary', 'bin')

        # Main initialization block:
        # - generate random ports,
        # - render configuration with these ports,
        # - start services and check if they are running okay,
        # - if anything is having problems, kill everything and start again.
        while True:

            # Generate random ports.
            zk_port = ServicesHolder.get_random_listener_port()
            kafka_envoy_port = ServicesHolder.get_random_listener_port()
            kafka_real_port1 = ServicesHolder.get_random_listener_port()
            kafka_real_port2 = ServicesHolder.get_random_listener_port()
            envoy_monitoring_port = ServicesHolder.get_random_listener_port()

            # These ports need to be exposed to tests.
            self.kafka_envoy_port = kafka_envoy_port
            self.kafka_real_port1 = kafka_real_port1
            self.kafka_real_port2 = kafka_real_port2
            self.envoy_monitoring_port = envoy_monitoring_port

            # Render config file for Envoy.
            template = RenderingHelper.get_template('envoy_config_yaml.j2')
            contents = template.render(
                data={
                    'kafka_envoy_port': kafka_envoy_port,
                    'kafka_real_port1': kafka_real_port1,
                    'kafka_real_port2': kafka_real_port2,
                    'envoy_monitoring_port': envoy_monitoring_port
                })
            envoy_config_file = os.path.join(config_dir, 'envoy_config.yaml')
            with open(envoy_config_file, 'w') as fd:
                fd.write(contents)
                print('Envoy config file rendered at: ' + envoy_config_file)

            # Render config file for Zookeeper.
            template = RenderingHelper.get_template('zookeeper_properties.j2')
            contents = template.render(data={'data_dir': zookeeper_store_dir, 'zk_port': zk_port})
            zookeeper_config_file = os.path.join(config_dir, 'zookeeper.properties')
            with open(zookeeper_config_file, 'w') as fd:
                fd.write(contents)
                print('Zookeeper config file rendered at: ' + zookeeper_config_file)

            # Render config file for Kafka cluster 1.
            template = RenderingHelper.get_template('kafka_server_properties.j2')
            contents = template.render(
                data={
                    'kafka_real_port': kafka_real_port1,
                    'data_dir': kafka_store_dir1,
                    'zk_port': zk_port,
                    'kafka_zk_instance': 'instance1'
                })
            kafka_config_file1 = os.path.join(config_dir, 'kafka_server1.properties')
            with open(kafka_config_file1, 'w') as fd:
                fd.write(contents)
                print('Kafka config file rendered at: ' + kafka_config_file1)

            # Render config file for Kafka cluster 2.
            template = RenderingHelper.get_template('kafka_server_properties.j2')
            contents = template.render(
                data={
                    'kafka_real_port': kafka_real_port2,
                    'data_dir': kafka_store_dir2,
                    'zk_port': zk_port,
                    'kafka_zk_instance': 'instance2'
                })
            kafka_config_file2 = os.path.join(config_dir, 'kafka_server2.properties')
            with open(kafka_config_file2, 'w') as fd:
                fd.write(contents)
                print('Kafka config file rendered at: ' + kafka_config_file2)

            # Start the services now.
            try:

                # Start Envoy in the background, pointing to rendered config file.
                envoy_binary = ServicesHolder.find_envoy()
                # --base-id is added to allow multiple Envoy instances to run at the same time.
                envoy_args = [
                    os.path.abspath(envoy_binary), '-c', envoy_config_file, '--base-id',
                    str(random.randint(1, 999999))
                ]
                envoy_handle = subprocess.Popen(
                    envoy_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.envoy_worker = ProcessWorker(
                    envoy_handle, 'Envoy', 'starting main dispatch loop')
                self.envoy_worker.await_startup()

                # Start Zookeeper in background, pointing to rendered config file.
                zk_binary = os.path.join(kafka_bin_dir, 'zookeeper-server-start.sh')
                zk_args = [os.path.abspath(zk_binary), zookeeper_config_file]
                zk_handle = subprocess.Popen(
                    zk_args,
                    env=launcher_environment,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)
                self.zk_worker = ProcessWorker(zk_handle, 'Zookeeper', 'binding to port')
                self.zk_worker.await_startup()

                self.kafka_workers = []

                # Start Kafka 1 in background, pointing to rendered config file.
                kafka_binary = os.path.join(kafka_bin_dir, 'kafka-server-start.sh')
                kafka_args = [os.path.abspath(kafka_binary), os.path.abspath(kafka_config_file1)]
                kafka_handle = subprocess.Popen(
                    kafka_args,
                    env=launcher_environment,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)
                kafka_worker = ProcessWorker(kafka_handle, 'Kafka', '[KafkaServer id=0] started')
                kafka_worker.await_startup()
                self.kafka_workers.append(kafka_worker)

                # Start Kafka 2 in background, pointing to rendered config file.
                kafka_binary = os.path.join(kafka_bin_dir, 'kafka-server-start.sh')
                kafka_args = [os.path.abspath(kafka_binary), os.path.abspath(kafka_config_file2)]
                kafka_handle = subprocess.Popen(
                    kafka_args,
                    env=launcher_environment,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)
                kafka_worker = ProcessWorker(kafka_handle, 'Kafka', '[KafkaServer id=0] started')
                kafka_worker.await_startup()
                self.kafka_workers.append(kafka_worker)

                # All services have started without problems - now we can finally finish.
                break

            except Exception as e:
                print('Could not start services, will try again', e)

                if self.kafka_workers:
                    self.kafka_worker.kill()
                    self.kafka_worker = None
                if self.zk_worker:
                    self.zk_worker.kill()
                    self.zk_worker = None
                if self.envoy_worker:
                    self.envoy_worker.kill()
                    self.envoy_worker = None

    @staticmethod
    def find_java():
        """
        This method just locates the Java installation in current directory.
        We cannot hardcode the name, as the dirname changes as per:
        https://github.com/bazelbuild/bazel/blob/master/tools/jdk/BUILD#L491
        """

        external_dir = os.path.join('.', 'external')
        for directory in os.listdir(external_dir):
            if 'remotejdk11' in directory:
                result = os.path.join(external_dir, directory, 'bin')
                print('Using Java: ' + result)
                return result
        raise Exception('Could not find Java in: ' + external_dir)

    @staticmethod
    def find_envoy():
        """
        This method locates envoy binary.
        It's present at ./contrib/exe/envoy-static (at least for mac/bazel-asan/bazel-tsan),
        or at ./external/envoy/contrib/exe/envoy-static (for bazel-compile_time_options).
        """

        candidate = os.path.join('.', 'contrib', 'exe', 'envoy-static')
        if os.path.isfile(candidate):
            return candidate
        candidate = os.path.join('.', 'external', 'envoy', 'contrib', 'exe', 'envoy-static')
        if os.path.isfile(candidate):
            return candidate
        raise Exception("Could not find Envoy")

    def shut_down(self):
        # Teardown - kill Kafka, Zookeeper, and Envoy. Then delete their data directory.
        print('Cleaning up')

        if self.kafka_workers:
            for worker in self.kafka_workers:
                worker.kill()

        if self.zk_worker:
            self.zk_worker.kill()

        if self.envoy_worker:
            self.envoy_worker.kill()

        if self.kafka_tmp_dir:
            print('Removing temporary directory: ' + self.kafka_tmp_dir)
            shutil.rmtree(self.kafka_tmp_dir)

    def check_state(self):
        self.envoy_worker.check_state()
        self.zk_worker.check_state()
        for worker in self.kafka_workers:
            worker.check_state()


class ProcessWorker:
    """
    Helper class that wraps the external service process.
    Provides ability to wait until service is ready to use (this is done by tracing logs) and
    printing service's output to stdout.
    """

    # Service is considered to be properly initialized after it has logged its startup message
    # and has been alive for INITIALIZATION_WAIT_SECONDS after that message has been seen.
    # This (clunky) design is needed because Zookeeper happens to log "binding to port" and then
    # might fail to bind.
    INITIALIZATION_WAIT_SECONDS = 3

    def __init__(self, process_handle, name, startup_message):
        # Handle to process and pretty name.
        self.process_handle = process_handle
        self.name = name

        self.startup_message = startup_message
        self.startup_message_ts = None

        # Semaphore raised when startup has finished and information regarding startup's success.
        self.initialization_semaphore = Semaphore(value=0)
        self.initialization_ok = False

        self.state_worker = Thread(target=ProcessWorker.initialization_worker, args=(self,))
        self.state_worker.start()
        self.out_worker = Thread(
            target=ProcessWorker.pipe_handler, args=(self, self.process_handle.stdout, 'out'))
        self.out_worker.start()
        self.err_worker = Thread(
            target=ProcessWorker.pipe_handler, args=(self, self.process_handle.stderr, 'err'))
        self.err_worker.start()

    @staticmethod
    def initialization_worker(owner):
        """
        Worker thread.
        Responsible for detecting if service died during initialization steps and ensuring if enough
        time has passed since the startup message has been seen.
        When either of these happens, we just raise the initialization semaphore.
        """

        while True:
            status = owner.process_handle.poll()
            if status:
                # Service died.
                print('%s did not initialize properly - finished with: %s' % (owner.name, status))
                owner.initialization_ok = False
                owner.initialization_semaphore.release()
                break
            else:
                # Service is still running.
                startup_message_ts = owner.startup_message_ts
                if startup_message_ts:
                    # The log message has been registered (by pipe_handler thread), let's just ensure that
                    # some time has passed and mark the service as running.
                    current_time = int(round(time.time()))
                    if current_time - startup_message_ts >= ProcessWorker.INITIALIZATION_WAIT_SECONDS:
                        print(
                            'Startup message seen %s seconds ago, and service is still running' %
                            (ProcessWorker.INITIALIZATION_WAIT_SECONDS),
                            flush=True)
                        owner.initialization_ok = True
                        owner.initialization_semaphore.release()
                        break
            time.sleep(1)
        print('Initialization worker for %s has finished' % (owner.name))

    @staticmethod
    def pipe_handler(owner, pipe, pipe_name):
        """
        Worker thread.
        If a service startup message is seen, then it just registers the timestamp of its appearance.
        Also prints every received message.
        """

        try:
            for raw_line in pipe:
                line = raw_line.decode().rstrip()
                print('%s(%s):' % (owner.name, pipe_name), line, flush=True)
                if owner.startup_message in line:
                    print(
                        '%s initialization message [%s] has been logged' %
                        (owner.name, owner.startup_message))
                    owner.startup_message_ts = int(round(time.time()))
        finally:
            pipe.close()
        print('Pipe handler for %s(%s) has finished' % (owner.name, pipe_name))

    def await_startup(self):
        """
        Awaits on initialization semaphore, and then verifies the initialization state.
        If everything is okay, we just continue (we can use the service), otherwise throw.
        """

        print('Waiting for %s to start...' % (self.name))
        self.initialization_semaphore.acquire()
        try:
            if self.initialization_ok:
                print('Service %s started successfully' % (self.name))
            else:
                raise Exception('%s could not start' % (self.name))
        finally:
            self.initialization_semaphore.release()

    def check_state(self):
        """
        Verifies if the service is still running. Throws if it is not.
        """

        status = self.process_handle.poll()
        if status:
            raise Exception('%s died with: %s' % (self.name, str(status)))

    def kill(self):
        """
        Utility method to kill the main service thread and all related workers.
        """

        print('Stopping service %s' % self.name)

        # Kill the real process.
        self.process_handle.kill()
        self.process_handle.wait()

        # The sub-workers are going to finish on their own, as they will detect main thread dying
        # (through pipes closing, or .poll() returning a non-null value).
        self.state_worker.join()
        self.out_worker.join()
        self.err_worker.join()

        print('Service %s has been stopped' % self.name)


class RenderingHelper:
    """
    Helper for jinja templates.
    """

    @staticmethod
    def get_template(template):
        import jinja2
        import os
        import sys
        # Templates are resolved relatively to main start script, due to main & test templates being
        # stored in different directories.
        env = jinja2.Environment(
            loader=jinja2.FileSystemLoader(searchpath=os.path.dirname(os.path.abspath(__file__))))
        return env.get_template(template)


if __name__ == '__main__':
    unittest.main()
