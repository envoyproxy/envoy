#!/usr/bin/env python3
"""
Test script for reverse connection socket interface functionality.

This script:
1. Starts two Envoy instances (on-prem and cloud) using Docker Compose
2. Starts the backend service (on-prem-service)
3. Initially starts on-prem without the reverse_conn_listener (removed from config)
4. Verifies reverse connections are not established by checking the cloud API
5. Adds the reverse_conn_listener to on-prem via xDS
6. Verifies reverse connections are established
7. Tests request routing through reverse connections
8. Stops and restarts cloud Envoy to test connection recovery
9. Verifies reverse connections are re-established
"""

import json
import time
import subprocess
import requests
import yaml
import tempfile
import os
import signal
import sys
import logging
from typing import Optional

# Configuration
CONFIG = {
    # File paths - use absolute paths based on script location
    'script_dir': os.path.dirname(os.path.abspath(__file__)),
    'docker_compose_file': os.path.join(os.path.dirname(os.path.abspath(__file__)), 'docker-compose.yaml'),
    'on_prem_config_file': os.path.join(os.path.dirname(os.path.abspath(__file__)), 'on-prem-envoy-custom-resolver.yaml'),
    'cloud_config_file': os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cloud-envoy.yaml'),
    
    # Ports
    'cloud_admin_port': 8889,
    'cloud_api_port': 9001,
    'cloud_egress_port': 8085,
    'on_prem_admin_port': 8888,
    'xds_server_port': 18000,  # Port for our xDS server
    
    # Container names
    'cloud_container': 'cloud-envoy',
    'on_prem_container': 'on-prem-envoy',
    
    # Timeouts
    'envoy_startup_timeout': 30,
    'docker_startup_delay': 10,
}

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class ReverseConnectionTester:
    def __init__(self):
        self.docker_compose_process: Optional[subprocess.Popen] = None
        self.temp_dir = tempfile.mkdtemp()
        self.docker_compose_dir = CONFIG['script_dir']
        self.current_compose_file = None  # Track which compose file is being used
        self.current_compose_cwd = None   # Track which directory to run from
        
    def create_on_prem_config_with_xds(self) -> str:
        """Create on-prem Envoy config with xDS for dynamic listener management."""
        # Load the original config
        with open(CONFIG['on_prem_config_file'], 'r') as f:
            config = yaml.safe_load(f)
        
        # Remove the reverse_conn_listener (will be added via xDS)
        listeners = config['static_resources']['listeners']
        config['static_resources']['listeners'] = [
            listener for listener in listeners 
            if listener['name'] != 'reverse_conn_listener'
        ]
        
        # Update the on-prem-service cluster to point to on-prem-service container
        for cluster in config['static_resources']['clusters']:
            if cluster['name'] == 'on-prem-service':
                cluster['load_assignment']['endpoints'][0]['lb_endpoints'][0]['endpoint']['address']['socket_address']['address'] = 'on-prem-service'
                cluster['load_assignment']['endpoints'][0]['lb_endpoints'][0]['endpoint']['address']['socket_address']['port_value'] = 80
        
        # Update the cloud cluster to point to cloud-envoy container
        for cluster in config['static_resources']['clusters']:
            if cluster['name'] == 'cloud':
                cluster['load_assignment']['endpoints'][0]['lb_endpoints'][0]['endpoint']['address']['socket_address']['address'] = 'cloud-envoy'
                cluster['load_assignment']['endpoints'][0]['lb_endpoints'][0]['endpoint']['address']['socket_address']['port_value'] = 9000
        
        # Add xDS cluster for dynamic configuration
        config['static_resources']['clusters'].append({
            'name': 'xds_cluster',
            'type': 'STRICT_DNS',
            'connect_timeout': '30s',
            'load_assignment': {
                'cluster_name': 'xds_cluster',
                'endpoints': [{
                    'lb_endpoints': [{
                        'endpoint': {
                            'address': {
                                'socket_address': {
                                    'address': 'xds-server',
                                    'port_value': CONFIG['xds_server_port']
                                }
                            }
                        }
                    }]
                }]
            },
            'dns_lookup_family': 'V4_ONLY'
        })
        
        # Add dynamic resources configuration
        config['dynamic_resources'] = {
            'lds_config': {
                'resource_api_version': 'V3',
                'api_config_source': {
                    'api_type': 'REST',
                    'transport_api_version': 'V3',
                    'cluster_names': ['xds_cluster'],
                    'refresh_delay': '1s'
                }
            }
        }
        
        config_file = os.path.join(self.temp_dir, "on-prem-envoy-with-xds.yaml")
        with open(config_file, 'w') as f:
            yaml.dump(config, f, default_flow_style=False)
        
        return config_file
    
    def start_docker_compose(self, on_prem_config: str = None) -> bool:
        """Start Docker Compose services."""
        logger.info("Starting Docker Compose services")
        
        # Create a temporary docker-compose file with the custom on-prem config if provided
        if on_prem_config:
            # Copy the original docker-compose file and modify it
            with open(CONFIG['docker_compose_file'], 'r') as f:
                compose_config = yaml.safe_load(f)
            
            # Update the on-prem-envoy service to use the custom config
            compose_config['services']['on-prem-envoy']['volumes'] = [
                f"{on_prem_config}:/etc/on-prem-envoy.yaml"
            ]
            
            # Copy cloud-envoy.yaml to temp directory and update the path
            import shutil
            temp_cloud_config = os.path.join(self.temp_dir, "cloud-envoy.yaml")
            shutil.copy(CONFIG['cloud_config_file'], temp_cloud_config)
            compose_config['services']['cloud-envoy']['volumes'] = [
                f"{temp_cloud_config}:/etc/cloud-envoy.yaml"
            ]
            
            # Copy Dockerfile.xds to temp directory
            dockerfile_xds = os.path.join(CONFIG['script_dir'], "Dockerfile.xds")
            temp_dockerfile_xds = os.path.join(self.temp_dir, "Dockerfile.xds")
            shutil.copy(dockerfile_xds, temp_dockerfile_xds)
            
            temp_compose_file = os.path.join(self.temp_dir, "docker-compose.yaml")
            with open(temp_compose_file, 'w') as f:
                yaml.dump(compose_config, f, default_flow_style=False)
            
            compose_file = temp_compose_file
        else:
            compose_file = CONFIG['docker_compose_file']
        
        # Start docker-compose in background with logs visible
        cmd = [
            "docker-compose", "-f", compose_file, "up"
        ]
        
        # If using a temporary compose file, run from temp directory, otherwise from docker_compose_dir
        if on_prem_config:
            # Run from temp directory where both files are located
            self.docker_compose_process = subprocess.Popen(
                cmd,
                cwd=self.temp_dir,
                universal_newlines=True
            )
            self.current_compose_file = compose_file
            self.current_compose_cwd = self.temp_dir
        else:
            # Run from original directory
            self.docker_compose_process = subprocess.Popen(
                cmd,
                cwd=self.docker_compose_dir,
                universal_newlines=True
            )
            self.current_compose_file = compose_file
            self.current_compose_cwd = self.docker_compose_dir
        
        # Wait a moment for containers to be ready
        time.sleep(CONFIG['docker_startup_delay'])
        
        # Check if process is still running
        if self.docker_compose_process.poll() is not None:
            logger.error("Docker Compose failed to start")
            return False
        
        return True
    
    def stop_docker_compose(self) -> bool:
        """Stop Docker Compose services."""
        logger.info("Stopping Docker Compose services")
        
        cmd = [
            "docker-compose", "-f", "docker-compose.yaml", "down"
        ]
        
        process = subprocess.Popen(
            cmd,
            cwd=self.docker_compose_dir,
            universal_newlines=True
        )
        
        process.wait()
        return process.returncode == 0
    
    def wait_for_envoy_ready(self, admin_port: int, name: str, timeout: int = 30) -> bool:
        """Wait for Envoy to be ready by checking admin endpoint."""
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                response = requests.get(f"http://localhost:{admin_port}/ready", timeout=1)
                if response.status_code == 200:
                    logger.info(f"{name} Envoy is ready")
                    return True
            except requests.exceptions.RequestException:
                pass
            
            time.sleep(1)
        
        logger.error(f"{name} Envoy failed to start within {timeout} seconds")
        return False
    
    def check_reverse_connections(self, api_port: int) -> bool:
        """Check if reverse connections are established by calling the cloud API."""
        try:
            # Check the reverse connections API on port 9001 (cloud-envoy's rev_conn_api_listener)
            response = requests.get(f"http://localhost:{api_port}/reverse_connections", timeout=5)
            if response.status_code == 200:
                data = response.json()
                logger.info(f"Reverse connections state: {data}")
                
                # Check if on-prem is connected
                if "connected" in data and "on-prem-node" in data["connected"]:
                    logger.info("Reverse connections are established")
                    return True
                else:
                    logger.info("Reverse connections are not established")
                    return False
            else:
                logger.error(f"Failed to get reverse connections state: {response.status_code}")
                logger.error(f"Response: {response.text}")
                return False
        except requests.exceptions.RequestException as e:
            logger.error(f"Error checking reverse connections: {e}")
            return False
        except json.JSONDecodeError as e:
            logger.error(f"Error parsing JSON response: {e}")
            logger.error(f"Response text: {response.text}")
            return False
    
    def test_reverse_connection_request(self, port: int) -> bool:
        """Test sending a request through reverse connection."""
        try:
            headers = {
                "x-remote-node-id": "on-prem-node",
                "x-dst-cluster-uuid": "on-prem"
            }
            # Use port 8081 (cloud-envoy's egress_listener) as specified in docker-compose
            response = requests.get(
                f"http://localhost:{port}/on_prem_service",
                headers=headers,
                timeout=10
            )
            
            if response.status_code == 200:
                logger.info(f"Reverse connection request successful: {response.text[:100]}...")
                return True
            else:
                logger.error(f"Reverse connection request failed: {response.status_code}")
                return False
        except requests.exceptions.RequestException as e:
            logger.error(f"Error testing reverse connection request: {e}")
            return False
    
    def get_reverse_conn_listener_config(self) -> dict:
        """Get the reverse_conn_listener configuration."""
        # Load the original config to extract the reverse_conn_listener
        with open(CONFIG['on_prem_config_file'], 'r') as f:
            config = yaml.safe_load(f)
        
        # Find the reverse_conn_listener
        for listener in config['static_resources']['listeners']:
            if listener['name'] == 'reverse_conn_listener':
                return listener
        
        raise Exception("reverse_conn_listener not found in config")
    
    def add_reverse_conn_listener_via_xds(self) -> bool:
        """Add reverse_conn_listener via xDS."""
        logger.info("Adding reverse_conn_listener via xDS")
        
        try:
            # Get the reverse_conn_listener configuration
            listener_config = self.get_reverse_conn_listener_config()
            
            # Send request to xDS server running in Docker
            import requests
            response = requests.post(
                f"http://localhost:{CONFIG['xds_server_port']}/add_listener",
                json={
                    'name': 'reverse_conn_listener',
                    'config': listener_config
                },
                timeout=10
            )
            
            if response.status_code == 200:
                logger.info("✓ reverse_conn_listener added via xDS")
                return True
            else:
                logger.error(f"Failed to add listener via xDS: {response.status_code}")
                return False
            
        except Exception as e:
            logger.error(f"Failed to add reverse_conn_listener via xDS: {e}")
            return False
    
    def remove_reverse_conn_listener_via_xds(self) -> bool:
        """Remove reverse_conn_listener via xDS."""
        logger.info("Removing reverse_conn_listener via xDS")
        
        try:
            # Send request to xDS server running in Docker
            import requests
            response = requests.post(
                f"http://localhost:{CONFIG['xds_server_port']}/remove_listener",
                json={
                    'name': 'reverse_conn_listener'
                },
                timeout=10
            )
            
            if response.status_code == 200:
                logger.info("✓ reverse_conn_listener removed via xDS")
                return True
            else:
                logger.error(f"Failed to remove listener via xDS: {response.status_code}")
                return False
            
        except Exception as e:
            logger.error(f"Failed to remove reverse_conn_listener via xDS: {e}")
            return False
    
    def get_container_name(self, service_name: str) -> str:
        """Get the actual container name for a service, handling Docker Compose suffixes."""
        try:
            result = subprocess.run(
                ['docker', 'ps', '--filter', f'name={service_name}', '--format', '{{.Names}}'],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                timeout=10
            )
            if result.returncode == 0 and result.stdout.strip():
                container_name = result.stdout.strip()
                logger.info(f"Found container name for service {service_name}: {container_name}")
                return container_name
            else:
                logger.error(f"Failed to find container for service {service_name}: {result.stderr}")
                return service_name  # Fallback to service name
        except Exception as e:
            logger.error(f"Error finding container name for {service_name}: {e}")
            return service_name  # Fallback to service name
    
    def check_container_network_status(self) -> bool:
        """Check the network status of containers to help debug DNS issues."""
        logger.info("Checking container network status")
        try:
            # Check if containers are running and their network info
            cmd = [
                'docker', 'ps', '--filter', 'name=envoy', '--format', 
                'table {{.Names}}\t{{.Status}}\t{{.Ports}}'
            ]
            
            result = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                timeout=10
            )
            
            if result.returncode == 0:
                logger.info("Container status:")
                logger.info(result.stdout)
            else:
                logger.error(f"Failed to get container status: {result.stderr}")
            
            # Check network info for the envoy-network
            cmd = ['docker', 'network', 'inspect', 'envoy-network']
            result = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                timeout=10
            )
            
            if result.returncode == 0:
                logger.info("Network info:")
                logger.info(result.stdout)
            else:
                logger.error(f"Failed to get network info: {result.stderr}")
            
            return True
        except Exception as e:
            logger.error(f"Error checking container network status: {e}")
            return False

    def check_network_connectivity(self) -> bool:
        """Check network connectivity from on-prem container to cloud container."""
        logger.info("Checking network connectivity from on-prem to cloud container")
        try:
            # First check container network status
            self.check_container_network_status()
            
            # Get the on-prem container name
            on_prem_container = self.get_container_name(CONFIG['on_prem_container'])
            
            # Test DNS resolution first
            logger.info("Testing DNS resolution...")
            dns_cmd = [
                'docker', 'exec', on_prem_container, 'sh', '-c',
                'nslookup cloud-envoy'
            ]
            
            dns_result = subprocess.run(
                dns_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                timeout=15
            )
            
            logger.info(f"DNS resolution result: {dns_result.stdout}")
            if dns_result.stderr:
                logger.error(f"DNS resolution error: {dns_result.stderr}")
            
            # Test ping connectivity
            logger.info("Testing ping connectivity...")
            ping_cmd = [
                'docker', 'exec', on_prem_container, 'sh', '-c',
                'ping -c 1 cloud-envoy'
            ]
            
            ping_result = subprocess.run(
                ping_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                timeout=15
            )
            
            logger.info(f"Ping result: {ping_result.stdout}")
            if ping_result.stderr:
                logger.error(f"Ping error: {ping_result.stderr}")
            
            # Test TCP connectivity to the specific port
            logger.info("Testing TCP connectivity to cloud-envoy:9000...")
            tcp_cmd = [
                'docker', 'exec', on_prem_container, 'sh', '-c',
                'nc -z cloud-envoy 9000'
            ]
            
            tcp_result = subprocess.run(
                tcp_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                timeout=15
            )
            
            logger.info(f"TCP connectivity result: {tcp_result.stdout}")
            if tcp_result.stderr:
                logger.error(f"TCP connectivity error: {tcp_result.stderr}")
            
            # Consider it successful if at least DNS resolution works
            if dns_result.returncode == 0:
                logger.info("✓ DNS resolution is working")
                return True
            else:
                logger.error("✗ DNS resolution failed")
                return False
                
        except Exception as e:
            logger.error(f"Error checking network connectivity: {e}")
            return False

    def start_cloud_envoy(self) -> bool:
        """Start the cloud Envoy container."""
        logger.info("Starting cloud Envoy container")
        try:
            # Use the same docker-compose file and directory that was used in start_docker_compose
            # This ensures the container is started with the same configuration
            if self.current_compose_file and self.current_compose_cwd:
                logger.info(f"Using stored compose file: {self.current_compose_file}")
                logger.info(f"Using stored compose directory: {self.current_compose_cwd}")
                compose_file = self.current_compose_file
                compose_cwd = self.current_compose_cwd
            else:
                logger.warn("No stored compose file found, using default")
                compose_file = CONFIG['docker_compose_file']
                compose_cwd = self.docker_compose_dir
            
            logger.info("Using docker-compose up to start cloud-envoy with consistent network config")
            result = subprocess.run(
                ['docker-compose', '-f', compose_file, 'up', '-d', CONFIG['cloud_container']],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                timeout=60,
                cwd=compose_cwd
            )
            
            if result.returncode == 0:
                logger.info("✓ Cloud Envoy container started")
                
                # Add a small delay to ensure network is properly established
                logger.info("Waiting for network to be established...")
                time.sleep(3)
                
                # Check network connectivity
                if not self.check_network_connectivity():
                    logger.warn("Network connectivity check failed, but continuing...")
                
                # Wait for cloud Envoy to be ready
                if not self.wait_for_envoy_ready(CONFIG['cloud_admin_port'], "cloud", CONFIG['envoy_startup_timeout']):
                    logger.error("Cloud Envoy failed to become ready after restart")
                    return False
                logger.info("✓ Cloud Envoy is ready after restart")
                return True
            else:
                logger.error(f"Failed to start cloud Envoy: {result.stderr}")
                return False
        except Exception as e:
            logger.error(f"Error starting cloud Envoy: {e}")
            return False
    
    def stop_cloud_envoy(self) -> bool:
        """Stop the cloud Envoy container."""
        logger.info("Stopping cloud Envoy container")
        try:
            container_name = self.get_container_name(CONFIG['cloud_container'])
            result = subprocess.run(
                ['docker', 'stop', container_name],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                timeout=30
            )
            if result.returncode == 0:
                logger.info("✓ Cloud Envoy container stopped")
                return True
            else:
                logger.error(f"Failed to stop cloud Envoy: {result.stderr}")
                return False
        except Exception as e:
            logger.error(f"Error stopping cloud Envoy: {e}")
            return False
    
    def run_test(self):
        """Run the complete reverse connection test."""
        try:
            logger.info("Starting reverse connection test")
            
            # Step 0: Start Docker Compose services with xDS config
            on_prem_config_with_xds = self.create_on_prem_config_with_xds()
            if not self.start_docker_compose(on_prem_config_with_xds):
                raise Exception("Failed to start Docker Compose services")
            
            # Step 1: Wait for Envoy instances to be ready
            if not self.wait_for_envoy_ready(CONFIG['cloud_admin_port'], "cloud", CONFIG['envoy_startup_timeout']):
                raise Exception("Cloud Envoy failed to start")
            
            if not self.wait_for_envoy_ready(CONFIG['on_prem_admin_port'], "on-prem", CONFIG['envoy_startup_timeout']):
                raise Exception("On-prem Envoy failed to start")
            
            # Step 2: Verify reverse connections are NOT established
            logger.info("Verifying reverse connections are NOT established")
            time.sleep(5)  # Give some time for any potential connections
            if self.check_reverse_connections(CONFIG['cloud_api_port']):  # cloud-envoy's API port
                raise Exception("Reverse connections should not be established without reverse_conn_listener")
            logger.info("✓ Reverse connections are correctly not established")
            
            # Step 3: Add reverse_conn_listener to on-prem via xDS
            logger.info("Adding reverse_conn_listener to on-prem via xDS")
            if not self.add_reverse_conn_listener_via_xds():
                raise Exception("Failed to add reverse_conn_listener via xDS")
            
            # Step 4: Wait for reverse connections to be established
            logger.info("Waiting for reverse connections to be established")
            max_wait = 60
            start_time = time.time()
            while time.time() - start_time < max_wait:
                if self.check_reverse_connections(CONFIG['cloud_api_port']):  # cloud-envoy's API port
                    logger.info("✓ Reverse connections are established")
                    break
                logger.info("Waiting for reverse connections to be established")
                time.sleep(1)
            else:
                raise Exception("Reverse connections failed to establish within timeout")
            
            # Step 5: Test request through reverse connection
            logger.info("Testing request through reverse connection")
            if not self.test_reverse_connection_request(CONFIG['cloud_egress_port']):  # cloud-envoy's egress port
                raise Exception("Reverse connection request failed")
            logger.info("✓ Reverse connection request successful")
            
            # Step 6: Stop cloud Envoy and verify reverse connections are down
            logger.info("Step 6: Stopping cloud Envoy to test connection recovery")
            if not self.stop_cloud_envoy():
                raise Exception("Failed to stop cloud Envoy")
            
            # Verify reverse connections are down
            logger.info("Verifying reverse connections are down after stopping cloud Envoy")
            time.sleep(2)  # Give some time for connections to be detected as down
            if self.check_reverse_connections(CONFIG['cloud_api_port']):
                logger.warn("Reverse connections still appear active after stopping cloud Envoy")
            else:
                logger.info("✓ Reverse connections are correctly down after stopping cloud Envoy")
            
            # Step 7: Wait for > drain timer (3s) and then start cloud Envoy
            logger.info("Step 7: Waiting for drain timer (3s) before starting cloud Envoy")
            time.sleep(15)  # Wait more than the reverse conn retry timer for the connections
            # to be drained.
            
            logger.info("Starting cloud Envoy to test reverse connection re-establishment")
            if not self.start_cloud_envoy():
                raise Exception("Failed to start cloud Envoy")
            
            # Step 8: Verify reverse connections are re-established
            logger.info("Step 8: Verifying reverse connections are re-established")
            max_wait = 60
            start_time = time.time()
            while time.time() - start_time < max_wait:
                if self.check_reverse_connections(CONFIG['cloud_api_port']):
                    logger.info("✓ Reverse connections are re-established after cloud Envoy restart")
                    break
                logger.info("Waiting for reverse connections to be re-established")
                time.sleep(1)
            else:
                raise Exception("Reverse connections failed to re-establish within timeout")
            
            # # Step 10: Remove reverse_conn_listener from on-prem via xDS
            logger.info("Removing reverse_conn_listener from on-prem via xDS")
            if not self.remove_reverse_conn_listener_via_xds():
                raise Exception("Failed to remove reverse_conn_listener via xDS")
            
            # # Step 11: Verify reverse connections are torn down
            logger.info("Verifying reverse connections are torn down")
            time.sleep(10)  # Wait for connections to be torn down
            if self.check_reverse_connections(CONFIG['cloud_api_port']):  # cloud-envoy's API port
                raise Exception("Reverse connections should be torn down after removing listener")
            logger.info("✓ Reverse connections are correctly torn down")
            
            logger.info("Test completed successfully!")
            return True
            
        except Exception as e:
            logger.error(f"Test failed: {e}")
            return False
        finally:
            self.cleanup()
    
    def cleanup(self):
        """Clean up processes and temporary files."""
        logger.info("Cleaning up")
        
        # Stop Docker Compose services
        if self.docker_compose_process:
            self.docker_compose_process.terminate()
            self.docker_compose_process.wait()
        
        self.stop_docker_compose()
        
        # Clean up temp directory
        import shutil
        shutil.rmtree(self.temp_dir, ignore_errors=True)

def main():
    """Main function."""
    tester = ReverseConnectionTester()
    
    # Handle Ctrl+C gracefully
    def signal_handler(sig, frame):
        logger.info("Received interrupt signal, cleaning up...")
        tester.cleanup()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    
    success = tester.run_test()
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main() 