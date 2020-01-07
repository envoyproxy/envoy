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

from kafka import KafkaAdminClient, KafkaConsumer, KafkaProducer, TopicPartition
from kafka.admin import ConfigResource, ConfigResourceType, NewPartitions, NewTopic
import urllib.request


class KafkaBrokerIntegrationTest(unittest.TestCase):
  """
  All tests in this class depend on Envoy/Zookeeper/Kafka running.
  For each of these tests we are going to create Kafka consumers/producers/admins and point them
  to Envoy (that proxies Kafka).
  We expect every operation to succeed (as they should reach Kafka) and the corresponding metrics
  to increase on Envoy side (to show that messages were received and forwarded successfully).
  """

  services = None

  @classmethod
  def setUpClass(cls):
    KafkaBrokerIntegrationTest.services = ServicesHolder()
    KafkaBrokerIntegrationTest.services.start()

  @classmethod
  def tearDownClass(cls):
    KafkaBrokerIntegrationTest.services.shut_down()

  def setUp(self):
    # We want to check if our services are okay before running any kind of test.
    KafkaBrokerIntegrationTest.services.check_state()
    self.metrics = MetricsHolder(self)

  def tearDown(self):
    # We want to check if our services are okay after running any test.
    KafkaBrokerIntegrationTest.services.check_state()

  @classmethod
  def kafka_address(cls):
    return '127.0.0.1:%s' % KafkaBrokerIntegrationTest.services.kafka_envoy_port

  @classmethod
  def envoy_stats_address(cls):
    return 'http://127.0.0.1:%s/stats' % KafkaBrokerIntegrationTest.services.envoy_monitoring_port

  def test_kafka_consumer_with_no_messages_received(self):
    """
    This test verifies that consumer sends fetches correctly, and receives nothing.
    """

    consumer = KafkaConsumer(bootstrap_servers=KafkaBrokerIntegrationTest.kafka_address(),
                             fetch_max_wait_ms=500)
    consumer.assign([TopicPartition('test_kafka_consumer_with_no_messages_received', 0)])
    for _ in range(10):
      records = consumer.poll(timeout_ms=1000)
      self.assertEqual(len(records), 0)

    self.metrics.collect_final_metrics()
    # 'consumer.poll()' can translate into 0 or more fetch requests.
    # We have set API timeout to 1000ms, while fetch_max_wait is 500ms.
    # This means that consumer will send roughly 2 (1000/500) requests per API call (so 20 total).
    # So increase of 10 (half of that value) should be safe enough to test.
    self.metrics.assert_metric_increase('fetch', 10)
    # Metadata is used by consumer to figure out current partition leader.
    self.metrics.assert_metric_increase('metadata', 1)

  def test_kafka_producer_and_consumer(self):
    """
    This test verifies that producer can send messages, and consumer can receive them.
    """

    messages_to_send = 100
    partition = TopicPartition('test_kafka_producer_and_consumer', 0)

    producer = KafkaProducer(bootstrap_servers=KafkaBrokerIntegrationTest.kafka_address())
    for _ in range(messages_to_send):
      future = producer.send(value=b'some_message_bytes',
                             topic=partition.topic,
                             partition=partition.partition)
      send_status = future.get()
      self.assertTrue(send_status.offset >= 0)

    consumer = KafkaConsumer(bootstrap_servers=KafkaBrokerIntegrationTest.kafka_address(),
                             auto_offset_reset='earliest',
                             fetch_max_bytes=100)
    consumer.assign([partition])
    received_messages = []
    while (len(received_messages) < messages_to_send):
      poll_result = consumer.poll(timeout_ms=1000)
      received_messages += poll_result[partition]

    self.metrics.collect_final_metrics()
    self.metrics.assert_metric_increase('metadata', 2)
    self.metrics.assert_metric_increase('produce', 100)
    # 'fetch_max_bytes' was set to a very low value, so client will need to send a FetchRequest
    # multiple times to broker to get all 100 messages (otherwise all 100 records could have been
    # received in one go).
    self.metrics.assert_metric_increase('fetch', 20)
    # Both producer & consumer had to fetch cluster metadata.
    self.metrics.assert_metric_increase('metadata', 2)

  def test_consumer_with_consumer_groups(self):
    """
    This test verifies that multiple consumers can form a Kafka consumer group.
    """

    consumer_count = 10
    consumers = []
    for id in range(consumer_count):
      consumer = KafkaConsumer(bootstrap_servers=KafkaBrokerIntegrationTest.kafka_address(),
                               group_id='test',
                               client_id='test-%s' % id)
      consumer.subscribe(['test_consumer_with_consumer_groups'])
      consumers.append(consumer)

    worker_threads = []
    for consumer in consumers:
      thread = Thread(target=KafkaBrokerIntegrationTest.worker, args=(consumer,))
      thread.start()
      worker_threads.append(thread)

    for thread in worker_threads:
      thread.join()

    for consumer in consumers:
      consumer.close()

    self.metrics.collect_final_metrics()
    self.metrics.assert_metric_increase('api_versions', consumer_count)
    self.metrics.assert_metric_increase('metadata', consumer_count)
    self.metrics.assert_metric_increase('join_group', consumer_count)
    self.metrics.assert_metric_increase('find_coordinator', consumer_count)
    self.metrics.assert_metric_increase('leave_group', consumer_count)

  @staticmethod
  def worker(consumer):
    """
    Worker thread for Kafka consumer.
    Multiple poll-s are done here, so that the group can safely form.
    """

    poll_operations = 10
    for i in range(poll_operations):
      consumer.poll(timeout_ms=1000)

  def test_admin_client(self):
    """
    This test verifies that Kafka Admin Client can still be used to manage Kafka.
    """

    admin_client = KafkaAdminClient(bootstrap_servers=KafkaBrokerIntegrationTest.kafka_address())

    # Create a topic with 3 partitions.
    new_topic_spec = NewTopic(name='test_admin_client', num_partitions=3, replication_factor=1)
    create_response = admin_client.create_topics([new_topic_spec])
    error_data = create_response.topic_errors
    self.assertEqual(len(error_data), 1)
    self.assertEqual(error_data[0], (new_topic_spec.name, 0, None))

    # Alter topic (change some Kafka-level property).
    config_resource = ConfigResource(ConfigResourceType.TOPIC, new_topic_spec.name,
                                     {'flush.messages': 42})
    alter_response = admin_client.alter_configs([config_resource])
    error_data = alter_response.resources
    self.assertEqual(len(error_data), 1)
    self.assertEqual(error_data[0][0], 0)

    # Add 2 more partitions to topic.
    new_partitions_spec = {new_topic_spec.name: NewPartitions(5)}
    new_partitions_response = admin_client.create_partitions(new_partitions_spec)
    error_data = create_response.topic_errors
    self.assertEqual(len(error_data), 1)
    self.assertEqual(error_data[0], (new_topic_spec.name, 0, None))

    # Delete a topic.
    delete_response = admin_client.delete_topics([new_topic_spec.name])
    error_data = create_response.topic_errors
    self.assertEqual(len(error_data), 1)
    self.assertEqual(error_data[0], (new_topic_spec.name, 0, None))

    self.metrics.collect_final_metrics()
    self.metrics.assert_metric_increase('create_topics', 1)
    self.metrics.assert_metric_increase('alter_configs', 1)
    self.metrics.assert_metric_increase('create_partitions', 1)
    self.metrics.assert_metric_increase('delete_topics', 1)


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

    stats_url = KafkaBrokerIntegrationTest.envoy_stats_address()
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
  Utility class for setting up our external dependencies: Envoy, Zookeeper & Kafka.
  """

  def __init__(self):
    self.kafka_tmp_dir = None

    self.envoy_worker = None
    self.zk_worker = None
    self.kafka_worker = None

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
    # This directory will store Kafka's data (== partitions).
    kafka_store_dir = self.kafka_tmp_dir + '/kafka_data'
    os.mkdir(kafka_store_dir)

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
      kafka_real_port = ServicesHolder.get_random_listener_port()
      kafka_envoy_port = ServicesHolder.get_random_listener_port()
      envoy_monitoring_port = ServicesHolder.get_random_listener_port()

      # These ports need to be exposed to tests.
      self.kafka_envoy_port = kafka_envoy_port
      self.envoy_monitoring_port = envoy_monitoring_port

      # Render config file for Envoy.
      template = RenderingHelper.get_template('envoy_config_yaml.j2')
      contents = template.render(
          data={
              'kafka_real_port': kafka_real_port,
              'kafka_envoy_port': kafka_envoy_port,
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

      # Render config file for Kafka.
      template = RenderingHelper.get_template('kafka_server_properties.j2')
      contents = template.render(
          data={
              'data_dir': kafka_store_dir,
              'zk_port': zk_port,
              'kafka_real_port': kafka_real_port,
              'kafka_envoy_port': kafka_envoy_port
          })
      kafka_config_file = os.path.join(config_dir, 'kafka_server.properties')
      with open(kafka_config_file, 'w') as fd:
        fd.write(contents)
        print('Kafka config file rendered at: ' + kafka_config_file)

      # Start the services now.
      try:

        # Start Envoy in the background, pointing to rendered config file.
        envoy_binary = ServicesHolder.find_envoy()
        # --base-id is added to allow multiple Envoy instances to run at the same time.
        envoy_args = [
            os.path.abspath(envoy_binary), '-c', envoy_config_file, '--base-id',
            str(random.randint(1, 999999))
        ]
        envoy_handle = subprocess.Popen(envoy_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.envoy_worker = ProcessWorker(envoy_handle, 'Envoy', 'starting main dispatch loop')
        self.envoy_worker.await_startup()

        # Start Zookeeper in background, pointing to rendered config file.
        zk_binary = os.path.join(kafka_bin_dir, 'zookeeper-server-start.sh')
        zk_args = [os.path.abspath(zk_binary), zookeeper_config_file]
        zk_handle = subprocess.Popen(zk_args,
                                     env=launcher_environment,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE)
        self.zk_worker = ProcessWorker(zk_handle, 'Zookeeper', 'binding to port')
        self.zk_worker.await_startup()

        # Start Kafka in background, pointing to rendered config file.
        kafka_binary = os.path.join(kafka_bin_dir, 'kafka-server-start.sh')
        kafka_args = [os.path.abspath(kafka_binary), kafka_config_file]
        kafka_handle = subprocess.Popen(kafka_args,
                                        env=launcher_environment,
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
        self.kafka_worker = ProcessWorker(kafka_handle, 'Kafka', '[KafkaServer id=0] started')
        self.kafka_worker.await_startup()

        # All services have started without problems - now we can finally finish.
        break

      except Exception as e:
        print('Could not start services, will try again', e)

        if self.kafka_worker:
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
    It's present at ./source/exe/envoy-static (at least for mac/bazel-asan/bazel-tsan),
    or at ./external/envoy/source/exe/envoy-static (for bazel-compile_time_options).
    """

    candidate = os.path.join('.', 'source', 'exe', 'envoy-static')
    if os.path.isfile(candidate):
      return candidate
    candidate = os.path.join('.', 'external', 'envoy', 'source', 'exe', 'envoy-static')
    if os.path.isfile(candidate):
      return candidate
    raise Exception("Could not find Envoy")

  def shut_down(self):
    # Teardown - kill Kafka, Zookeeper, and Envoy. Then delete their data directory.
    print('Cleaning up')

    if self.kafka_worker:
      self.kafka_worker.kill()

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
    self.kafka_worker.check_state()


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
    self.out_worker = Thread(target=ProcessWorker.pipe_handler,
                             args=(self, self.process_handle.stdout, 'out'))
    self.out_worker.start()
    self.err_worker = Thread(target=ProcessWorker.pipe_handler,
                             args=(self, self.process_handle.stderr, 'err'))
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
            print('Startup message seen %s seconds ago, and service is still running' %
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
          print('%s initialization message [%s] has been logged' %
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
    env = jinja2.Environment(loader=jinja2.FileSystemLoader(
        searchpath=os.path.dirname(os.path.abspath(__file__))))
    return env.get_template(template)


if __name__ == '__main__':
  unittest.main()
