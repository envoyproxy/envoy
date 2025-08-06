#!/usr/bin/env python3
"""
Example management server that demonstrates bidirectional xDS with dynamic listener creation.

This shows how a management server can use the same ADS stream to:
1. Send dynamic listener configuration TO Envoy (normal xDS)
2. Request status FROM Envoy (reverse xDS)
3. Wait for the listener to be ready

The key insight is that we use the same stream and message types,
just with reversed roles for reverse xDS.
"""

import grpc
import time
import threading
from concurrent import futures
from typing import Dict, Any, Optional
from google.protobuf.any_pb2 import Any

# Generated from envoy API protos
import envoy.service.discovery.v3.discovery_pb2 as discovery_pb2
import envoy.service.discovery.v3.ads_pb2_grpc as ads_grpc
import envoy.config.listener.v3.listener_pb2 as listener_pb2
import envoy.config.core.v3.address_pb2 as address_pb2
import envoy.config.core.v3.socket_option_pb2 as socket_option_pb2
import envoy.extensions.filters.network.http_connection_manager.v3.http_connection_manager_pb2 as hcm_pb2
import envoy.extensions.filters.http.router.v3.router_pb2 as router_pb2
import envoy.config.route.v3.route_pb2 as route_pb2


class BidirectionalAdsServer:
    """
    Example ADS server that demonstrates bidirectional xDS.
    
    This server:
    1. Accepts ADS connections from Envoy clients  
    2. Sends dynamic listener configuration (normal xDS)
    3. Requests client status (reverse xDS)
    4. Waits for listener to be ready
    5. Handles status responses from clients
    """
    
    def __init__(self, port: int = 18000):
        self.port = port
        self.server = None
        self.clients = {}  # client_id -> stream mapping
        self.listener_status = {}  # listener_name -> status
        self.pending_listeners = set()  # Listeners waiting to be ready
        
    def start(self):
        """Start the ADS server."""
        self.server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
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
    
    def create_dynamic_listener(self, name: str = "dynamic_listener", port: int = 8080) -> listener_pb2.Listener:
        """Create a dynamic HTTP listener configuration."""
        
        # Create listener
        listener = listener_pb2.Listener()
        listener.name = name
        
        # Configure address
        socket_address = address_pb2.SocketAddress()
        socket_address.protocol = address_pb2.SocketAddress.TCP
        socket_address.address = "0.0.0.0"
        socket_address.port_value = port
        
        listener.address.socket_address.CopyFrom(socket_address)
        
        # Create HTTP connection manager filter
        hcm = hcm_pb2.HttpConnectionManager()
        hcm.stat_prefix = "ingress_http"
        hcm.codec_type = hcm_pb2.HttpConnectionManager.AUTO
        
        # Configure route
        route_config = route_pb2.RouteConfiguration()
        route_config.name = "local_route"
        
        virtual_host = route_config.virtual_hosts.add()
        virtual_host.name = "local_service"
        virtual_host.domains.extend(["*"])
        
        route = virtual_host.routes.add()
        route.match.prefix = "/"
        route.route.cluster = "local_cluster"
        
        hcm.route_config.CopyFrom(route_config)
        
        # Add router filter
        router_filter = hcm.http_filters.add()
        router_filter.name = "envoy.filters.http.router"
        router_typed_config = Any()
        router_typed_config.Pack(router_pb2.Router())
        router_filter.typed_config.CopyFrom(router_typed_config)
        
        # Create network filter
        filter_config = listener.filter_chains.add().filters.add()
        filter_config.name = "envoy.filters.network.http_connection_manager"
        
        typed_config = Any()
        typed_config.Pack(hcm)
        filter_config.typed_config.CopyFrom(typed_config)
        
        return listener


class BidirectionalAdsServicer(ads_grpc.AggregatedDiscoveryServiceServicer):
    """ADS servicer that handles bidirectional xDS."""
    
    def __init__(self, server):
        self.server = server
        self.nonce_counter = 0
        self.streams = {}  # client_id -> response_stream mapping
        self.listener_config_sent = False
        
    def StreamAggregatedResources(self, request_iterator, context):
        """Handle bidirectional ADS stream."""
        client_id = context.peer()
        print(f"\n=== New client connected: {client_id} ===")
        
        # Store the response stream for this client
        response_queue = []
        
        def monitor_listener_status():
            """Monitor listener status and wait for readiness."""
            time.sleep(3)  # Wait for listener config to be processed
            
            print(f"\n=== Monitoring listener status for {client_id} ===")
            
            # Send reverse xDS request to get listener status
            reverse_request = discovery_pb2.DiscoveryRequest()
            reverse_request.type_url = "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus"
            reverse_request.version_info = ""  # Initial request
            reverse_request.node = discovery_pb2.Node()
            reverse_request.node.id = "management-server"
            reverse_request.node.cluster = "management-cluster"
            reverse_request.resource_names.append("dynamic_listener")  # Monitor our specific listener
            
            print(f"Requesting status for dynamic_listener...")
            response_queue.append(reverse_request)
            
            # Monitor until listener is ready
            max_attempts = 30  # 30 seconds timeout
            for attempt in range(max_attempts):
                time.sleep(1)
                if "dynamic_listener" in self.server.listener_status:
                    status = self.server.listener_status["dynamic_listener"]
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
            print(f"    Type URL: {request.type_url}")
            print(f"    Version: {request.version_info}")
            print(f"    Nonce: {request.response_nonce}")
            print(f"    Resources requested: {len(request.resource_names)} items")
            if request.resource_names:
                for i, resource_name in enumerate(request.resource_names):
                    print(f"      [{i+1}] {resource_name}")
            else:
                print(f"      (all resources for type)")
            
            # Log node information if present
            if hasattr(request, 'node') and request.node:
                print(f"    Node ID: {getattr(request.node, 'id', 'unknown')}")
                print(f"    Node Cluster: {getattr(request.node, 'cluster', 'unknown')}")
            
            # Log error details if present
            if hasattr(request, 'error_detail') and request.error_detail:
                print(f"    ❌ Error Detail: {request.error_detail}")
            
            response = self.handle_client_message(request, client_id)
            if response:
                print(f"📤 [Response #{message_count}] Sending to {client_id}")
                print(f"    Type URL: {response.type_url}")
                print(f"    Version: {response.version_info}")
                print(f"    Nonce: {response.nonce}")
                print(f"    Resources sent: {len(response.resources)} items")
                yield response
            else:
                print(f"📤 [Response #{message_count}] No response needed for {client_id}")
            print(f"    ───────────────────────────────────────")
            
            # Send any queued reverse requests
            while response_queue:
                reverse_req = response_queue.pop(0)
                print(f"🔄 Sending REVERSE REQUEST to {client_id}")
                print(f"    Type URL: {reverse_req.type_url}")
                print(f"    Version: {reverse_req.version_info}")
                print(f"    Resources requested: {len(reverse_req.resource_names)} items")
                if reverse_req.resource_names:
                    for i, resource_name in enumerate(reverse_req.resource_names):
                        print(f"        [{i+1}] {resource_name}")
                print(f"    Node ID: {getattr(reverse_req.node, 'id', 'unknown')}")
                yield reverse_req
                
        print(f"Client {client_id} disconnected")
        
    def handle_client_message(self, request, client_id):
        """Handle a message from the client."""
        
        print(f"🔍 Processing message type: {request.type_url}")
        
        if request.type_url.startswith("type.googleapis.com/envoy.config."):
            # Normal xDS request - client asking for configuration
            print(f"    → Handling as CONFIG REQUEST")
            return self.handle_config_request(request, client_id)
            
        elif request.type_url.startswith("type.googleapis.com/envoy.admin."):
            # This would be a reverse xDS response - client providing status
            print(f"    → Handling as STATUS RESPONSE")
            self.handle_status_response(request, client_id)
            return self.create_status_ack(request)
            
        else:
            print(f"    → ❓ UNKNOWN REQUEST TYPE: {request.type_url}")
            print(f"    → Returning empty response")
            return None
    
    def handle_config_request(self, request, client_id):
        """Handle normal xDS configuration request."""
        print(f"⚙️  Config request from {client_id}")
        print(f"    Request type: {request.type_url}")
        print(f"    Request version: {request.version_info}")
        print(f"    Request nonce: {request.response_nonce}")
        
        response = discovery_pb2.DiscoveryResponse()
        response.type_url = request.type_url
        response.version_info = "1"
        response.nonce = self.generate_nonce()
        
        # Handle LDS (Listener Discovery Service) requests
        if request.type_url == "type.googleapis.com/envoy.config.listener.v3.Listener":
            print(f"    → Processing LISTENER DISCOVERY SERVICE (LDS) request")
            if not self.listener_config_sent:
                print(f"    → First LDS request - creating dynamic listener")
                print(f"🚀 Sending dynamic listener configuration to {client_id}")
                
                # Create and send dynamic listener
                listener = self.server.create_dynamic_listener("dynamic_listener", 8080)
                self.server.pending_listeners.add("dynamic_listener")
                
                # Pack the listener into the response
                resource = Any()
                resource.Pack(listener)
                response.resources.append(resource)
                
                self.listener_config_sent = True
                print(f"✅ Dynamic listener configuration sent! Listener will bind to port 8080")
                print(f"    → Response will contain 1 listener resource")
            else:
                # Already sent, send empty response (no changes)
                print(f"    → Subsequent LDS request - no changes, sending empty response")
                print(f"    → Response will contain 0 listener resources")
                
        # Handle other config types (CDS, RDS, EDS) with empty responses for now
        elif request.type_url in [
            "type.googleapis.com/envoy.config.cluster.v3.Cluster",
            "type.googleapis.com/envoy.config.route.v3.RouteConfiguration", 
            "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment"
        ]:
            # Send empty config for these types
            service_name = {
                "type.googleapis.com/envoy.config.cluster.v3.Cluster": "CLUSTER DISCOVERY SERVICE (CDS)",
                "type.googleapis.com/envoy.config.route.v3.RouteConfiguration": "ROUTE DISCOVERY SERVICE (RDS)",
                "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment": "ENDPOINT DISCOVERY SERVICE (EDS)"
            }.get(request.type_url, "UNKNOWN SERVICE")
            print(f"    → Processing {service_name} request")
            print(f"    → Sending empty response (no configuration for this demo)")
        else:
            print(f"    → ❓ Unknown config type: {request.type_url}")
        
        print(f"    → Response ready with {len(response.resources)} resources")
        return response
    
    def handle_status_response(self, request, client_id):
        """Handle reverse xDS status response from client."""
        print(f"\n📊 === RECEIVED STATUS RESPONSE from {client_id} ===")
        print(f"    Status type: {request.type_url}")
        print(f"    Status version: {request.version_info}")
        print(f"    Status nonce: {request.response_nonce}")
        print(f"    Status resources: {len(request.resources)} items")
        
        # Log raw message details
        print(f"    📋 Raw message details:")
        print(f"        Has node: {hasattr(request, 'node') and request.node is not None}")
        if hasattr(request, 'node') and request.node:
            print(f"        Node ID: {getattr(request.node, 'id', 'N/A')}")
            print(f"        Node cluster: {getattr(request.node, 'cluster', 'N/A')}")
        print(f"        Has error_detail: {hasattr(request, 'error_detail') and request.error_detail is not None}")
        if hasattr(request, 'error_detail') and request.error_detail:
            print(f"        Error: {request.error_detail}")
        
        # Parse listener status resources
        if request.type_url == "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus":
            print(f"    → Processing LISTENER READINESS STATUS")
            
            if len(request.resources) == 0:
                print(f"    → ⚠️  No status resources provided")
            
            for i, resource in enumerate(request.resources):
                print(f"    📄 Listener Status Resource {i + 1}:")
                print(f"        Type: {getattr(resource, 'type_url', 'unknown')}")
                print(f"        Value size: {len(getattr(resource, 'value', b''))} bytes")
                
                # Log the raw bytes for debugging
                if hasattr(resource, 'value') and resource.value:
                    try:
                        # Try to display first few bytes as hex
                        hex_preview = ' '.join(f'{b:02x}' for b in resource.value[:20])
                        if len(resource.value) > 20:
                            hex_preview += '...'
                        print(f"        Value (hex): {hex_preview}")
                    except Exception as e:
                        print(f"        Value preview error: {e}")
                
                # For this example, we'll simulate parsing the status
                # In a real implementation, you would unpack the protobuf:
                # 
                # import envoy.admin.v3.reverse_xds_pb2 as reverse_xds_pb2
                # if resource.type_url.endswith("ListenerReadinessStatus"):
                #     status = reverse_xds_pb2.ListenerReadinessStatus()
                #     resource.Unpack(status)
                #     self.server.listener_status[status.listener_name] = {
                #         "ready": status.ready,
                #         "address": status.bound_address,
                #         "state": status.state,
                #         "error": status.error_message
                #     }
                
                # For demonstration, simulate listener becoming ready
                if "dynamic_listener" in self.server.pending_listeners:
                    print(f"    📡 Simulating listener status parsing: dynamic_listener is now ready!")
                    self.server.listener_status["dynamic_listener"] = {
                        "ready": True,
                        "address": "0.0.0.0:8080",
                        "state": "READY",
                        "error": "",
                        "raw_resource_size": len(getattr(resource, 'value', b''))
                    }
                    self.server.pending_listeners.discard("dynamic_listener")
                    print(f"    ✅ Listener marked as ready in server state")
                else:
                    print(f"    ℹ️  Listener not in pending list (already processed or different listener)")
        else:
            # Handle other status types
            print(f"    → Processing OTHER STATUS TYPE: {request.type_url}")
            for i, resource in enumerate(request.resources):
                print(f"    📄 Status Resource {i + 1}:")
                print(f"        Type: {getattr(resource, 'type_url', 'unknown')}")
                print(f"        Value size: {len(getattr(resource, 'value', b''))} bytes")
        
        print(f"    📊 Status processing complete")
    
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
    """Run the bidirectional ADS server example with dynamic listener creation."""
    server = BidirectionalAdsServer()
    
    try:
        server.start()
        
        print("\n🚀 Bidirectional xDS Server with Dynamic Listener Creation")
        print("=" * 60)
        print("The flow will be:")
        print("1. 📡 Envoy connects with ADS stream")
        print("2. 📋 Envoy requests LDS configuration (normal xDS)")
        print("3. 🎯 Server sends dynamic listener config (port 8080)")  
        print("4. ⚙️  Envoy creates and binds the listener")
        print("5. 🔍 Server requests listener status (reverse xDS)")
        print("6. 📊 Envoy sends listener readiness status")
        print("7. ✅ Server confirms listener is ready")
        print("8. 🎉 Dynamic listener is live and accepting traffic!")
        print("\n💡 You can test the listener with:")
        print("   curl http://localhost:8080/")
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
Enhanced Dynamic Listener Example Message Flow:

1. Connection & Configuration (Normal xDS):
   Envoy → DiscoveryRequest { type_url: "type.googleapis.com/envoy.config.listener.v3.Listener" }
   Server → DiscoveryResponse { 
     type_url: "type.googleapis.com/envoy.config.listener.v3.Listener",
     resources: [packed dynamic_listener config for port 8080]
   }
   Envoy → DiscoveryRequest { ACK }

2. Listener Status Monitoring (Reverse xDS):  
   Server → DiscoveryRequest { 
     type_url: "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus",
     resource_names: ["dynamic_listener"]
   }
   Envoy → DiscoveryResponse { 
     type_url: "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus",
     resources: [packed ListenerReadinessStatus with ready=true, address="0.0.0.0:8080"]
   }
   Server → DiscoveryRequest { ACK }

3. Result:
   ✅ Dynamic listener is created and bound to port 8080
   ✅ Server knows the listener is ready via reverse xDS
   ✅ Traffic can now flow to http://localhost:8080/

Key advantages:
- Single stream for both directions
- Same message types and semantics
- Real-time listener status monitoring
- Dynamic configuration without restarts
- Reuses existing authentication
- No additional network connections
- Much simpler than separate gRPC servers
""" 