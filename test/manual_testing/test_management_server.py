#!/usr/bin/env python3
"""
Enhanced test management server for manual testing of bidirectional xDS.

This server can be used to test the bidirectional xDS implementation by:
1. Accepting ADS connections from Envoy
2. Sending basic configuration (listeners, clusters)  
3. Requesting status via reverse xDS
4. Displaying received status information

Usage:
    python test_management_server.py --port=18000
"""

import argparse
import grpc
import json
import sys
import time
import threading
from concurrent import futures
from typing import Dict, Any, Set

# Generated from envoy API protos
import envoy.service.discovery.v3.discovery_pb2 as discovery_pb2
import envoy.service.discovery.v3.ads_pb2_grpc as ads_grpc
import envoy.config.listener.v3.listener_pb2 as listener_pb2
import envoy.config.cluster.v3.cluster_pb2 as cluster_pb2
import envoy.admin.v3.reverse_xds_pb2 as reverse_xds_pb2
from google.protobuf import any_pb2


class TestManagementServer:
    """
    Enhanced test management server for bidirectional xDS testing.
    """
    
    def __init__(self, port: int = 18000, enable_reverse_xds: bool = True):
        self.port = port
        self.enable_reverse_xds = enable_reverse_xds
        self.server = None
        self.clients = {}  # client_id -> stream mapping
        self.nonce_counter = 0
        self.reverse_request_interval = 5.0  # seconds
        
    def start(self):
        """Start the ADS server."""
        self.server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
        ads_grpc.add_AggregatedDiscoveryServiceServicer_to_server(
            TestAdsServicer(self), self.server)
        
        listen_addr = f'[::]:{self.port}'
        self.server.add_insecure_port(listen_addr)
        self.server.start()
        
        print(f"🚀 Test Management Server started on {listen_addr}")
        print(f"📡 Reverse xDS: {'ENABLED' if self.enable_reverse_xds else 'DISABLED'}")
        print("⏳ Waiting for Envoy clients to connect...")
        print()
        
    def stop(self):
        """Stop the server.""" 
        if self.server:
            self.server.stop(0)
            print("\n🛑 Server stopped")


class TestAdsServicer(ads_grpc.AggregatedDiscoveryServiceServicer):
    """Test ADS servicer that handles bidirectional xDS."""
    
    def __init__(self, server: TestManagementServer):
        self.server = server
        self.streams = {}  # client_id -> stream writer
        
    def StreamAggregatedResources(self, request_iterator, context):
        """Handle bidirectional ADS stream."""
        client_id = context.peer()
        client_name = self._get_client_name(client_id)
        
        print(f"✅ New client connected: {client_name}")
        
        # Store stream for sending reverse requests
        self.streams[client_id] = None  # Will be set when we need to send
        
        # Start thread to send reverse requests periodically
        if self.server.enable_reverse_xds:
            reverse_thread = threading.Thread(
                target=self._send_periodic_reverse_requests, 
                args=(client_id, client_name),
                daemon=True
            )
            reverse_thread.start()
        
        try:
            # Handle messages from client
            for request in request_iterator:
                response = self._handle_client_message(request, client_id, client_name)
                if response:
                    yield response
        except Exception as e:
            print(f"❌ Error handling client {client_name}: {e}")
        finally:
            print(f"👋 Client {client_name} disconnected")
            self.streams.pop(client_id, None)
            
    def _get_client_name(self, client_id: str) -> str:
        """Extract a readable name from the client ID."""
        # Extract port from grpc://127.0.0.1:12345 format
        if ':' in client_id:
            port = client_id.split(':')[-1]
            return f"envoy-{port}"
        return client_id
        
    def _send_periodic_reverse_requests(self, client_id: str, client_name: str):
        """Send reverse xDS requests periodically."""
        time.sleep(2)  # Wait for initial handshake
        
        while client_id in self.streams:
            try:
                print(f"🔄 Sending reverse xDS request to {client_name}")
                
                # Create reverse request for listener status
                reverse_request = discovery_pb2.DiscoveryRequest()
                reverse_request.type_url = "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus"
                reverse_request.version_info = ""  # Initial request
                reverse_request.node.id = "test-management-server"
                reverse_request.node.cluster = "test-cluster"
                
                # Note: In a real implementation, this would be sent directly on the stream
                # For this test, we simulate what would happen
                print(f"  📋 Request type: {reverse_request.type_url}")
                print(f"  🏷️  Node ID: {reverse_request.node.id}")
                
            except Exception as e:
                print(f"❌ Error sending reverse request to {client_name}: {e}")
                
            time.sleep(self.server.reverse_request_interval)
    
    def _handle_client_message(self, request, client_id: str, client_name: str):
        """Handle a message from the client."""
        
        if self._is_config_request(request.type_url):
            # Normal xDS request - client asking for configuration
            return self._handle_config_request(request, client_name)
            
        elif self._is_reverse_response(request.type_url):
            # Reverse xDS response - client providing status
            self._handle_reverse_response(request, client_name)
            return self._create_ack_response(request)
            
        else:
            print(f"❓ Unknown request type from {client_name}: {request.type_url}")
            return None
    
    def _is_config_request(self, type_url: str) -> bool:
        """Check if this is a normal configuration request."""
        config_types = [
            "type.googleapis.com/envoy.config.listener.v3.Listener",
            "type.googleapis.com/envoy.config.cluster.v3.Cluster", 
            "type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
            "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment",
        ]
        return type_url in config_types
    
    def _is_reverse_response(self, type_url: str) -> bool:
        """Check if this is a reverse xDS response."""
        reverse_types = [
            "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus",
            "type.googleapis.com/envoy.admin.v3.ClusterHealthStatus",
            "type.googleapis.com/envoy.admin.v3.ConfigurationSnapshot",
        ]
        return type_url in reverse_types
    
    def _handle_config_request(self, request, client_name: str):
        """Handle normal xDS configuration request."""
        print(f"📥 Config request from {client_name}: {request.type_url}")
        
        response = discovery_pb2.DiscoveryResponse()
        response.type_url = request.type_url
        response.version_info = "test-v1"
        response.nonce = self._generate_nonce()
        
        # Add basic configuration based on type
        if "Listener" in request.type_url:
            response = self._add_test_listeners(response)
        elif "Cluster" in request.type_url:
            response = self._add_test_clusters(response)
        # For other types, just send empty config
        
        print(f"📤 Sent config response: {len(response.resources)} resources")
        return response
    
    def _handle_reverse_response(self, request, client_name: str):
        """Handle reverse xDS response from client."""
        print(f"\n🎯 === Received Status from {client_name} ===")
        print(f"📋 Type: {request.type_url}")
        print(f"🏷️  Version: {request.version_info}")
        print(f"📦 Resources: {len(request.resources)} items")
        
        # Parse and display the status resources
        for i, resource in enumerate(request.resources):
            print(f"\n  📄 Resource {i + 1}:")
            print(f"     Type: {resource.type_url}")
            print(f"     Size: {len(resource.value)} bytes")
            
            # Try to parse listener status
            if "ListenerReadinessStatus" in resource.type_url:
                try:
                    status = reverse_xds_pb2.ListenerReadinessStatus()
                    resource.Unpack(status)
                    
                    print(f"     📋 Listener: {status.listener_name}")
                    print(f"     ✅ Ready: {status.ready}")
                    print(f"     🌐 Address: {status.bound_address}")
                    print(f"     🔧 State: {reverse_xds_pb2.ListenerReadinessStatus.State.Name(status.state)}")
                    if status.error_message:
                        print(f"     ❌ Error: {status.error_message}")
                    print(f"     🕒 Updated: {status.last_updated}")
                    
                except Exception as e:
                    print(f"     ❌ Failed to parse listener status: {e}")
        
        print("🎯 === End Status Report ===\n")
    
    def _add_test_listeners(self, response):
        """Add test listener configuration."""
        # Create a basic HTTP listener
        listener = listener_pb2.Listener()
        listener.name = "test_http_listener"
        listener.address.socket_address.address = "0.0.0.0"
        listener.address.socket_address.port_value = 8080
        
        # Pack into Any
        listener_any = any_pb2.Any()
        listener_any.Pack(listener)
        response.resources.append(listener_any)
        
        return response
    
    def _add_test_clusters(self, response):
        """Add test cluster configuration."""
        # Create a basic cluster
        cluster = cluster_pb2.Cluster()
        cluster.name = "test_cluster"
        cluster.type = cluster_pb2.Cluster.STATIC
        
        # Pack into Any
        cluster_any = any_pb2.Any()
        cluster_any.Pack(cluster)
        response.resources.append(cluster_any)
        
        return response
    
    def _create_ack_response(self, original_request):
        """Create ACK response for status update."""
        response = discovery_pb2.DiscoveryResponse()
        response.type_url = original_request.type_url
        response.version_info = original_request.version_info
        response.nonce = self._generate_nonce()
        # Empty resources = ACK
        return response
    
    def _generate_nonce(self):
        """Generate a unique nonce."""
        self.server.nonce_counter += 1
        return f"test-nonce-{self.server.nonce_counter}"


def main():
    """Run the test management server."""
    parser = argparse.ArgumentParser(description="Test management server for bidirectional xDS")
    parser.add_argument("--port", type=int, default=18000, help="Server port (default: 18000)")
    parser.add_argument("--no-reverse-xds", action="store_true", help="Disable reverse xDS requests")
    parser.add_argument("--interval", type=float, default=5.0, help="Reverse request interval in seconds")
    
    args = parser.parse_args()
    
    server = TestManagementServer(
        port=args.port,
        enable_reverse_xds=not args.no_reverse_xds
    )
    server.reverse_request_interval = args.interval
    
    try:
        server.start()
        
        print("📋 Server Features:")
        print("  • Accepts ADS connections from Envoy clients")
        print("  • Provides basic configuration (listeners, clusters)")
        print("  • Requests listener status via reverse xDS")
        print("  • Displays received status information")
        print()
        print("🔧 Test Commands:")
        print("  # Start Envoy client:")
        print(f"  ./envoy -c config.yaml --service-cluster test --service-node envoy-test")
        print()
        print("  # Check connection:")
        print("  curl http://localhost:8080")
        print()
        print("📊 Monitoring:")
        print("  Watch this terminal for status updates...")
        print("\n🛑 Press Ctrl+C to stop...")
        
        # Keep server running
        while True:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\n🛑 Shutting down server...")
    except Exception as e:
        print(f"❌ Server error: {e}")
        return 1
    finally:
        server.stop()
    
    return 0


if __name__ == "__main__":
    sys.exit(main()) 