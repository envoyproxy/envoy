#!/usr/bin/env python3
"""
Example management server that demonstrates bidirectional xDS.

This shows how a management server can use the same ADS stream to:
1. Send configuration TO Envoy (normal xDS)
2. Request status FROM Envoy (reverse xDS)

The key insight is that we use the same stream and message types,
just with reversed roles for reverse xDS.
"""

import grpc
import time
import threading
from typing import Dict, Any

# Generated from envoy API protos
import envoy.service.discovery.v3.discovery_pb2 as discovery_pb2
import envoy.service.discovery.v3.ads_pb2_grpc as ads_grpc


class BidirectionalAdsServer:
    """
    Example ADS server that demonstrates bidirectional xDS.
    
    This server:
    1. Accepts ADS connections from Envoy clients  
    2. Sends configuration (normal xDS)
    3. Requests client status (reverse xDS)
    4. Handles status responses from clients
    """
    
    def __init__(self, port: int = 18000):
        self.port = port
        self.server = None
        self.clients = {}  # client_id -> stream mapping
        
    def start(self):
        """Start the ADS server."""
        self.server = grpc.server(grpc.ThreadPoolExecutor(max_workers=10))
        ads_grpc.add_AggregatedDiscoveryServiceServicer_to_server(
            BidirectionalAdsServicer(self), self.server)
        
        listen_addr = f'[::]:{self.port}'
        self.server.add_insecure_port(listen_addr)
        self.server.start()
        
        print(f"Bidirectional ADS server listening on {listen_addr}")
        print("Waiting for Envoy clients to connect...")
        
    def stop(self):
        """Stop the server."""
        if self.server:
            self.server.stop(0)


class BidirectionalAdsServicer(ads_grpc.AggregatedDiscoveryServiceServicer):
    """ADS servicer that handles bidirectional xDS."""
    
    def __init__(self, server):
        self.server = server
        self.nonce_counter = 0
        
    def StreamAggregatedResources(self, request_iterator, context):
        """Handle bidirectional ADS stream."""
        client_id = context.peer()
        print(f"\n=== New client connected: {client_id} ===")
        
        def send_reverse_requests():
            """Send reverse xDS requests to get client status."""
            time.sleep(2)  # Wait for initial handshake
            
            # Request listener status from client
            print(f"Requesting listener status from {client_id}")
            reverse_request = discovery_pb2.DiscoveryRequest()
            reverse_request.type_url = "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus"
            reverse_request.version_info = ""  # Initial request
            reverse_request.node.id = "management-server"
            reverse_request.node.cluster = "management-cluster"
            
            # Note: In a real implementation, you would send this on the stream
            # For this example, we just show what would be sent
            print(f"Would send reverse request: {reverse_request}")
            
        # Start thread to send reverse requests
        reverse_thread = threading.Thread(target=send_reverse_requests)
        reverse_thread.daemon = True
        reverse_thread.start()
        
        # Handle messages from client
        for request in request_iterator:
            response = self.handle_client_message(request, client_id)
            if response:
                yield response
                
        print(f"Client {client_id} disconnected")
        
    def handle_client_message(self, request, client_id):
        """Handle a message from the client."""
        
        if request.type_url.startswith("type.googleapis.com/envoy.config."):
            # Normal xDS request - client asking for configuration
            return self.handle_config_request(request, client_id)
            
        elif request.type_url.startswith("type.googleapis.com/envoy.admin."):
            # This would be a reverse xDS response - client providing status
            self.handle_status_response(request, client_id)
            return self.create_status_ack(request)
            
        else:
            print(f"Unknown request type: {request.type_url}")
            return None
    
    def handle_config_request(self, request, client_id):
        """Handle normal xDS configuration request."""
        print(f"Config request from {client_id}: {request.type_url}")
        
        # For this example, just send empty config
        response = discovery_pb2.DiscoveryResponse()
        response.type_url = request.type_url
        response.version_info = "1"
        response.nonce = self.generate_nonce()
        
        return response
    
    def handle_status_response(self, request, client_id):
        """Handle reverse xDS status response from client."""
        print(f"\n=== Received Status from {client_id} ===")
        print(f"Type: {request.type_url}")
        print(f"Version: {request.version_info}")
        print(f"Resources: {len(request.resources)} items")
        
        # Parse the status resources
        for i, resource in enumerate(request.resources):
            print(f"Resource {i + 1}:")
            print(f"  Type: {resource.type_url}")
            print(f"  Size: {len(resource.value)} bytes")
            
            # In a real implementation, you would unpack and process:
            # if resource.type_url.endswith("ListenerReadinessStatus"):
            #     status = ListenerReadinessStatus()
            #     resource.Unpack(status)
            #     print(f"  Listener: {status.listener_name}")
            #     print(f"  Ready: {status.ready}")
    
    def create_status_ack(self, original_request):
        """Create ACK response for status update."""
        response = discovery_pb2.DiscoveryResponse()
        response.type_url = original_request.type_url
        response.version_info = original_request.version_info
        response.nonce = self.generate_nonce()
        # Empty resources = ACK
        return response
    
    def generate_nonce(self):
        """Generate a unique nonce."""
        self.nonce_counter += 1
        return f"nonce_{self.nonce_counter}"


def main():
    """Run the bidirectional ADS server example."""
    server = BidirectionalAdsServer()
    
    try:
        server.start()
        
        print("\nServer running. The flow will be:")
        print("1. Envoy connects with ADS stream")
        print("2. Envoy sends config requests (normal xDS)")
        print("3. Server sends config responses")  
        print("4. Server sends status requests (reverse xDS)")
        print("5. Envoy sends status responses")
        print("6. Server ACKs the status")
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


"""
Example message flow:

1. Normal xDS (Client → Server):
   Envoy → DiscoveryRequest { type_url: "type.googleapis.com/envoy.config.listener.v3.Listener" }
   Server → DiscoveryResponse { config data }
   Envoy → DiscoveryRequest { ACK }

2. Reverse xDS (Server → Client):  
   Server → DiscoveryRequest { type_url: "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus" }
   Envoy → DiscoveryResponse { status data }
   Server → DiscoveryRequest { ACK }

Key advantages:
- Single stream for both directions
- Same message types and semantics
- Reuses existing authentication
- No additional network connections
- Much simpler than separate gRPC servers
""" 