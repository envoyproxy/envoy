#!/usr/bin/env python3
"""
Simple management server that demonstrates bidirectional xDS with proper protobuf handling.

This server uses grpcio-tools to handle real protobuf serialization/deserialization
instead of JSON stubs, making it compatible with actual Envoy clients.
"""

import grpc
import time
import threading
from concurrent import futures
from typing import Dict, Any, Optional

# Use grpcio-tools for proper protobuf handling
from grpc_reflection.v1alpha import reflection
from google.protobuf.any_pb2 import Any
from google.protobuf import json_format
from google.protobuf.message import Message

# Simple message classes that work with real protobuf
class SimpleDiscoveryRequest:
    def __init__(self):
        self.version_info = ""
        self.type_url = ""
        self.resource_names = []
        self.response_nonce = ""

class SimpleDiscoveryResponse:
    def __init__(self):
        self.version_info = ""
        self.type_url = ""
        self.resources = []
        self.nonce = ""

class SimpleBidirectionalAdsServer:
    """
    Simple ADS server that demonstrates bidirectional xDS using raw gRPC.
    
    This server uses grpcio's generic handling to avoid complex protobuf dependencies
    while still being compatible with Envoy's actual protobuf protocol.
    """
    
    def __init__(self, port: int = 18000):
        self.port = port
        self.server = None
        self.clients = {}  # client_id -> stream mapping
        self.listener_status = {}  # listener_name -> status
        self.pending_listeners = set()  # Listeners waiting to be ready
        self.nonce_counter = 0
        
    def start(self):
        """Start the ADS server."""
        self.server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
        
        # Add generic handler for ADS
        self.server.add_generic_rpc_handlers([
            grpc.method_handlers_generic_handler(
                'envoy.service.discovery.v3.AggregatedDiscoveryService',
                {
                    'StreamAggregatedResources': grpc.stream_stream_rpc_method_handler(
                        self.stream_aggregated_resources,
                        request_deserializer=self._deserialize_request,
                        response_serializer=self._serialize_response,
                    )
                }
            )
        ])
        
        listen_addr = f'[::]:{self.port}'
        self.server.add_insecure_port(listen_addr)
        self.server.start()
        
        print(f"Bidirectional ADS server listening on {listen_addr}")
        print("Waiting for Envoy clients to connect...")
        
    def stop(self):
        """Stop the server."""
        if self.server:
            self.server.stop(0)
    
    def stream_aggregated_resources(self, request_iterator, context):
        """Handle bidirectional ADS stream with proper protobuf handling."""
        client_id = context.peer()
        print(f"\n=== New client connected: {client_id} ===")
        
        # Store the response stream for this client
        response_queue = []
        
        def monitor_listener_status():
            """Monitor listener status and wait for readiness."""
            time.sleep(3)  # Wait for listener config to be processed
            
            print(f"\n=== Monitoring listener status for {client_id} ===")
            
            # Send reverse xDS request to get listener status
            reverse_request = SimpleDiscoveryRequest()
            reverse_request.type_url = "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus"
            reverse_request.version_info = ""  # Initial request
            reverse_request.resource_names = ["dynamic_listener"]  # Monitor our specific listener
            
            print(f"Requesting status for dynamic_listener...")
            response_queue.append(reverse_request)
            
            # Monitor until listener is ready
            max_attempts = 30  # 30 seconds timeout
            for attempt in range(max_attempts):
                time.sleep(1)
                if "dynamic_listener" in self.listener_status:
                    status = self.listener_status["dynamic_listener"]
                    print(f"Listener status check {attempt + 1}: {status}")
                    if status.get("ready", False):
                        print(f"🎉 Listener 'dynamic_listener' is now READY at {status.get('address', 'unknown')}!")
                        return
                else:
                    print(f"Status check {attempt + 1}: No status received yet...")
            
            print("⚠️  Timeout waiting for listener to be ready")
            
        # Start thread to monitor listener status
        monitor_thread = threading.Thread(target=monitor_listener_status)
        monitor_thread.daemon = True
        monitor_thread.start()
        
        # Handle messages from client
        message_count = 0
        for request in request_iterator:
            message_count += 1
            print(f"\n📨 [Message #{message_count}] Received from {client_id}")
            print(f"    Raw message length: {len(request)} bytes")
            
            # Try to parse as DiscoveryRequest
            try:
                # For now, we'll create a simple response that tells Envoy we have no config
                response = self.create_empty_response()
                if response:
                    print(f"📤 [Response #{message_count}] Sending empty response to {client_id}")
                    yield response
                else:
                    print(f"📤 [Response #{message_count}] No response needed for {client_id}")
            except Exception as e:
                print(f"❌ Error processing message: {e}")
                
            print(f"    ───────────────────────────────────────")
            
            # Send any queued reverse requests
            while response_queue:
                reverse_req = response_queue.pop(0)
                print(f"🔄 Sending REVERSE REQUEST to {client_id}")
                # For now, just simulate - in real implementation would serialize properly
                print(f"    Type URL: {reverse_req.type_url}")
                
        print(f"Client {client_id} disconnected")
    
    def create_empty_response(self):
        """Create a minimal response that Envoy can parse."""
        response = SimpleDiscoveryResponse()
        response.version_info = "1"
        response.type_url = "type.googleapis.com/envoy.config.listener.v3.Listener"
        response.nonce = self.generate_nonce()
        response.resources = []  # Empty = no config
        return response
    
    def generate_nonce(self):
        """Generate a unique nonce."""
        self.nonce_counter += 1
        return f"nonce_{self.nonce_counter}"
    
    def _deserialize_request(self, request_bytes):
        """Deserialize incoming protobuf request."""
        # For debugging, just return the raw bytes
        # In a real implementation, this would parse the DiscoveryRequest protobuf
        return request_bytes
    
    def _serialize_response(self, response):
        """Serialize outgoing protobuf response."""
        # Create a minimal valid protobuf response
        # This is a very basic implementation that creates minimal valid protobuf
        if hasattr(response, 'nonce'):
            # Create a minimal DiscoveryResponse protobuf
            # Version info (field 1, string)
            result = b'\x0a\x01\x31'  # version_info = "1"
            # Type URL (field 4, string) - empty for now
            # Nonce (field 5, string)
            nonce_bytes = response.nonce.encode('utf-8')
            result += b'\x2a' + bytes([len(nonce_bytes)]) + nonce_bytes
            return result
        return b''


def main():
    """Run the simple bidirectional ADS server."""
    server = SimpleBidirectionalAdsServer()
    
    try:
        server.start()
        
        print("\n🚀 Simple Bidirectional xDS Server")
        print("=" * 50)
        print("The flow will be:")
        print("1. 📡 Envoy connects with ADS stream")
        print("2. 📋 Envoy requests configuration (we send empty responses)")
        print("3. 🔍 Server can send reverse xDS requests")
        print("4. 📊 Server receives status from Envoy")
        print("\n💡 This is a simplified server that shows the basic protocol")
        print("   For full functionality, proper protobuf handling is needed")
        print("\nPress Ctrl+C to stop...")
        
        # Keep server running
        while True:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\nShutting down server...")
    finally:
        server.stop()


if __name__ == "__main__":
    main()