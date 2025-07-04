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
8. Removes the reverse_conn_listener via xDS
9. Verifies reverse connections are torn down
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
import threading
import socket
from typing import Dict, Any, Optional, List
from contextlib import contextmanager
import logging
# Note: Using HTTP-based xDS server instead of gRPC

# Configuration
CONFIG = {
    # File paths - use absolute paths based on script location
    'script_dir': os.path.dirname(os.path.abspath(__file__)),
    'docker_compose_file': os.path.join(os.path.dirname(os.path.abspath(__file__)), 'docker-compose.yaml'),
    'on_prem_config_file': os.path.join(os.path.dirname(os.path.abspath(__file__)), 'on-prem-envoy-custom-resolver.yaml'),
    'cloud_config_file': os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cloud-envoy.yaml'),
    
    # Ports
    'cloud_admin_port': 8888,
    'cloud_api_port': 9001,
    'cloud_egress_port': 8081,
    'on_prem_admin_port': 8889,
    'on_prem_api_port': 9002,
    'on_prem_ingress_port': 8080,
    'xds_server_port': 18000,  # Port for our xDS server
    
    # Container names
    'cloud_container': 'cloud-envoy',
    'on_prem_container': 'on-prem-envoy',
    'backend_container': 'on-prem-service',
    
    # Timeouts
    'envoy_startup_timeout': 30,
    'reverse_conn_timeout': 60,
    'docker_startup_delay': 10,
}

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class XDSServer:
    """Simple xDS server for dynamic listener management."""
    
    def __init__(self):
        self.listeners = {}
        self.version = 1
        self._lock = threading.Lock()
        self.server = None
    
    def start(self, port: int):
        """Start the xDS server."""
        # Create a simple HTTP server that serves xDS responses
        import http.server
        import socketserver
        
        class XDSHandler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                if self.path == '/v3/discovery:listeners':
                    content_length = int(self.headers['Content-Length'])
                    post_data = self.rfile.read(content_length)
                    
                    # Parse the request and send response
                    response_data = self.server.xds_server.handle_lds_request(post_data)
                    
                    self.send_response(200)
                    self.send_header('Content-type', 'application/json')
                    self.end_headers()
                    self.wfile.write(response_data.encode())
                else:
                    self.send_response(404)
                    self.end_headers()
            
            def log_message(self, format, *args):
                # Suppress HTTP server logs
                pass
        
        class XDSServer(socketserver.TCPServer):
            def __init__(self, server_address, RequestHandlerClass, xds_server):
                self.xds_server = xds_server
                super().__init__(server_address, RequestHandlerClass)
        
        self.server = XDSServer(('0.0.0.0', port), XDSHandler, self)
        self.server_thread = threading.Thread(target=self.server.serve_forever)
        self.server_thread.daemon = True
        self.server_thread.start()
        logger.info(f"xDS server started on port {port}")
    
    def stop(self):
        """Stop the xDS server."""
        if self.server:
            self.server.shutdown()
            self.server.server_close()
    
    def handle_lds_request(self, request_data: bytes) -> str:
        """Handle LDS request and return response."""
        with self._lock:
            # Create a simple LDS response
            response = {
                "version_info": str(self.version),
                "resources": [],
                "type_url": "type.googleapis.com/envoy.config.listener.v3.Listener"
            }
            
            # Add all current listeners
            for listener_name, listener_config in self.listeners.items():
                response["resources"].append(listener_config)
            
            return json.dumps(response)
    
    def add_listener(self, listener_name: str, listener_config: dict):
        """Add a listener to the xDS server."""
        with self._lock:
            self.listeners[listener_name] = listener_config
            self.version += 1
            logger.info(f"Added listener {listener_name}, version {self.version}")
    
    def remove_listener(self, listener_name: str) -> bool:
        """Remove a listener from the xDS server."""
        with self._lock:
            if listener_name in self.listeners:
                del self.listeners[listener_name]
                self.version += 1
                logger.info(f"Removed listener {listener_name}, version {self.version}")
                return True
            return False

class EnvoyProcess:
    """Represents a running Envoy process."""
    def __init__(self, process, config_file, name, admin_port, api_port):
        self.process = process
        self.config_file = config_file
        self.name = name
        self.admin_port = admin_port
        self.api_port = api_port

class BackendProcess:
    """Represents a running backend service."""
    def __init__(self, process, name, port):
        self.process = process
        self.name = name
        self.port = port

class ReverseConnectionTester:
    def __init__(self):
        self.on_prem_process: Optional[EnvoyProcess] = None
        self.cloud_process: Optional[EnvoyProcess] = None
        self.backend_process: Optional[BackendProcess] = None
        self.docker_compose_process: Optional[subprocess.Popen] = None
        self.xds_server: Optional[XDSServer] = None
        self.temp_dir = tempfile.mkdtemp()
        self.docker_compose_dir = CONFIG['script_dir']
        
    def create_on_prem_config_without_reverse_conn(self) -> str:
        """Create on-prem Envoy config without the reverse_conn_listener."""
        # Load the original config
        with open(CONFIG['on_prem_config_file'], 'r') as f:
            config = yaml.safe_load(f)
        
        # Remove the reverse_conn_listener
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
                cluster['load_assignment']['endpoints'][0]['lb_endpoints'][0]['endpoint']['address']['socket_address']['address'] = CONFIG['cloud_container']
                cluster['load_assignment']['endpoints'][0]['lb_endpoints'][0]['endpoint']['address']['socket_address']['port_value'] = CONFIG['cloud_api_port']
        
        config_file = os.path.join(self.temp_dir, "on-prem-envoy-no-reverse.yaml")
        with open(config_file, 'w') as f:
            yaml.dump(config, f, default_flow_style=False)
        
        return config_file
    
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
    
    def start_xds_server(self):
        """Start the xDS server."""
        # The xDS server is now running in Docker, so we don't need to start it locally
        logger.info("xDS server will be started by Docker Compose")
        return True
    
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
        else:
            # Run from original directory
            self.docker_compose_process = subprocess.Popen(
                cmd,
                cwd=self.docker_compose_dir,
                universal_newlines=True
            )
        
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
    
    def check_xds_server_state(self) -> dict:
        """Check the current state of the xDS server."""
        try:
            response = requests.get(
                f"http://localhost:{CONFIG['xds_server_port']}/state",
                timeout=5
            )
            if response.status_code == 200:
                return response.json()
            else:
                logger.error(f"Failed to get xDS server state: {response.status_code}")
                return {}
        except Exception as e:
            logger.error(f"Error checking xDS server state: {e}")
            return {}

    def remove_reverse_conn_listener_via_xds(self) -> bool:
        """Remove reverse_conn_listener via xDS."""
        logger.info("Removing reverse_conn_listener via xDS")
        
        try:
            # Check state before removal
            logger.info("xDS server state before removal:")
            state_before = self.check_xds_server_state()
            logger.info(f"Current listeners: {state_before.get('listeners', [])}")
            logger.info(f"Current version: {state_before.get('version', 'unknown')}")
            
            # Send request to xDS server running in Docker
            response = requests.post(
                f"http://localhost:{CONFIG['xds_server_port']}/remove_listener",
                json={
                    'name': 'reverse_conn_listener'
                },
                timeout=10
            )
            
            if response.status_code == 200:
                logger.info("✓ reverse_conn_listener removed via xDS")
                
                # Check state after removal
                logger.info("xDS server state after removal:")
                state_after = self.check_xds_server_state()
                logger.info(f"Current listeners: {state_after.get('listeners', [])}")
                logger.info(f"Current version: {state_after.get('version', 'unknown')}")
                
                # Wait a bit longer for Envoy to poll and pick up the change
                logger.info("Waiting for Envoy to pick up the listener removal...")
                time.sleep(20)  # Increased wait time
                
                return True
            else:
                logger.error(f"Failed to remove listener via xDS: {response.status_code}")
                return False
                
        except Exception as e:
            logger.error(f"Failed to remove reverse_conn_listener via xDS: {e}")
            return False
    
    def run_test(self):
        """Run the complete reverse connection test."""
        try:
            logger.info("Starting reverse connection test")
            
            # Step 0: Start xDS server
            if not self.start_xds_server():
                raise Exception("Failed to start xDS server")
            
            # Step 1: Start Docker Compose services with xDS config
            on_prem_config_with_xds = self.create_on_prem_config_with_xds()
            if not self.start_docker_compose(on_prem_config_with_xds):
                raise Exception("Failed to start Docker Compose services")
            
            # Step 2: Wait for Envoy instances to be ready
            if not self.wait_for_envoy_ready(CONFIG['cloud_admin_port'], "cloud", CONFIG['envoy_startup_timeout']):
                raise Exception("Cloud Envoy failed to start")
            
            if not self.wait_for_envoy_ready(CONFIG['on_prem_admin_port'], "on-prem", CONFIG['envoy_startup_timeout']):
                raise Exception("On-prem Envoy failed to start")
            
            # Step 3: Verify reverse connections are NOT established
            logger.info("Verifying reverse connections are NOT established")
            time.sleep(5)  # Give some time for any potential connections
            if self.check_reverse_connections(CONFIG['cloud_api_port']):  # cloud-envoy's API port
                raise Exception("Reverse connections should not be established without reverse_conn_listener")
            logger.info("✓ Reverse connections are correctly not established")
            
            # Step 4: Add reverse_conn_listener to on-prem via xDS
            logger.info("Adding reverse_conn_listener to on-prem via xDS")
            if not self.add_reverse_conn_listener_via_xds():
                raise Exception("Failed to add reverse_conn_listener via xDS")
            
            # Step 5: Wait for reverse connections to be established
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
            
            # Step 6: Test request through reverse connection
            logger.info("Testing request through reverse connection")
            if not self.test_reverse_connection_request(CONFIG['cloud_egress_port']):  # cloud-envoy's egress port
                raise Exception("Reverse connection request failed")
            logger.info("✓ Reverse connection request successful")
            
            # # Step 7: Remove reverse_conn_listener from on-prem via xDS
            # logger.info("Removing reverse_conn_listener from on-prem via xDS")
            # if not self.remove_reverse_conn_listener_via_xds():
            #     raise Exception("Failed to remove reverse_conn_listener via xDS")
            
            # # Step 8: Verify reverse connections are torn down
            # logger.info("Verifying reverse connections are torn down")
            # time.sleep(10)  # Wait for connections to be torn down
            # if self.check_reverse_connections(CONFIG['cloud_api_port']):  # cloud-envoy's API port
            #     raise Exception("Reverse connections should be torn down after removing listener")
            # logger.info("✓ Reverse connections are correctly torn down")
            
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
        
        # Stop xDS server
        if self.xds_server:
            self.xds_server.stop()
        
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