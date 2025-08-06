#!/usr/bin/env python3

"""
Simple Bidirectional xDS Management Server

This is a simplified version that doesn't require complex protobuf dependencies.
It demonstrates the core concepts of bidirectional xDS using basic gRPC.
"""

import logging
import time
import threading
import json
from concurrent import futures
from dataclasses import dataclass
from typing import Dict, Set, Optional

import grpc
from google.protobuf import any_pb2
from google.protobuf import struct_pb2


# Simple message classes to avoid complex protobuf dependencies
@dataclass
class DiscoveryRequest:
    version_info: str = ""
    node_id: str = ""
    type_url: str = ""
    resource_names: list = None
    response_nonce: str = ""
    error_detail: Optional[str] = None

    def __post_init__(self):
        if self.resource_names is None:
            self.resource_names = []


@dataclass
class DiscoveryResponse:
    version_info: str = ""
    resources: list = None
    canary: bool = False
    type_url: str = ""
    nonce: str = ""
    control_plane_identifier: str = "simple-bidirectional-xds"

    def __post_init__(self):
        if self.resources is None:
            self.resources = []


class SimpleBidirectionalAdsServer:
    """Simple server that tracks basic listener state"""
    
    def __init__(self):
        self.listener_status: Dict[str, str] = {}
        self.pending_listeners: Set[str] = set()
        
    def create_simple_listener_config(self) -> dict:
        """Create a basic listener configuration"""
        return {
            "name": "dynamic_listener",
            "address": {
                "socket_address": {
                    "address": "0.0.0.0",
                    "port_value": 8080
                }
            },
            "filter_chains": [{
                "filters": [{
                    "name": "envoy.filters.network.http_connection_manager",
                    "typed_config": {
                        "@type": "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager",
                        "stat_prefix": "dynamic_http",
                        "codec_type": "AUTO",
                        "route_config": {
                            "name": "local_route",
                            "virtual_hosts": [{
                                "name": "local_service",
                                "domains": ["*"],
                                "routes": [{
                                    "match": {"prefix": "/"},
                                    "direct_response": {
                                        "status": 200,
                                        "body": {
                                            "inline_string": "Hello from dynamic listener!\n"
                                        }
                                    }
                                }]
                            }]
                        },
                        "http_filters": [{
                            "name": "envoy.filters.http.router",
                            "typed_config": {
                                "@type": "type.googleapis.com/envoy.extensions.filters.http.router.v3.Router"
                            }
                        }]
                    }
                }]
            }]
        }


class SimpleBidirectionalAdsServicer:
    """Simplified ADS servicer for bidirectional communication"""
    
    def __init__(self, server: SimpleBidirectionalAdsServer):
        self.server = server
        self.streams = []
        self.listener_config_sent = False
        self.nonce_counter = 0

    def get_next_nonce(self) -> str:
        self.nonce_counter += 1
        return f"nonce-{self.nonce_counter}"

    def StreamAggregatedResources(self, request_iterator, context):
        """Handle bidirectional ADS stream"""
        print("🔗 New client connected to ADS stream")
        
        # Store this stream for sending reverse requests
        response_queue = []
        
        def monitor_listener_status():
            """Thread to monitor listener status and send reverse requests"""
            time.sleep(2)  # Wait for initial config to be sent
            
            if "dynamic_listener" not in self.server.listener_status:
                print("📊 Requesting listener status via reverse xDS...")
                
                # Create a reverse request for listener status
                reverse_req = DiscoveryResponse(
                    version_info="1",
                    type_url="type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus",
                    nonce=self.get_next_nonce(),
                    resources=[]  # Empty resources means we're requesting status
                )
                response_queue.append(reverse_req)
                
                # Poll for status updates
                timeout = 30
                start_time = time.time()
                while time.time() - start_time < timeout:
                    if self.server.listener_status.get("dynamic_listener") == "READY":
                        print("✅ Listener 'dynamic_listener' is now READY at 0.0.0.0:8080")
                        print("🎉 Test with: curl http://localhost:8080/")
                        break
                    time.sleep(1)
                else:
                    print("⏰ Timeout waiting for listener to become ready")

        # Start monitoring thread
        monitor_thread = threading.Thread(target=monitor_listener_status, daemon=True)
        monitor_thread.start()

        try:
            for request_data in request_iterator:
                # Parse the request (simplified)
                if hasattr(request_data, 'type_url'):
                    type_url = request_data.type_url
                    resource_names = list(request_data.resource_names) if hasattr(request_data, 'resource_names') else []
                    
                    print(f"📥 Received request: {type_url}")
                    
                    if "Listener" in type_url:
                        response = self.handle_listener_request(request_data)
                        if response:
                            yield response
                    elif "ListenerReadinessStatus" in type_url:
                        self.handle_status_response(request_data)
                    else:
                        # Send empty response for other types
                        empty_response = self.create_empty_response(type_url)
                        yield empty_response

                # Send any queued reverse requests
                while response_queue:
                    reverse_req = response_queue.pop(0)
                    print(f"📤 Sending reverse request: {reverse_req.type_url}")
                    # Convert to proper protobuf message
                    yield self.convert_to_protobuf_response(reverse_req)

        except Exception as e:
            print(f"❌ Error in ADS stream: {e}")
        finally:
            print("🔌 Client disconnected from ADS stream")

    def handle_listener_request(self, request):
        """Handle LDS requests"""
        if not self.listener_config_sent:
            print("📋 Sending dynamic listener configuration...")
            
            # Create listener config
            listener_config = self.server.create_simple_listener_config()
            
            # Create response
            response = DiscoveryResponse(
                version_info="1",
                type_url="type.googleapis.com/envoy.config.listener.v3.Listener",
                nonce=self.get_next_nonce(),
                resources=[self.pack_resource(listener_config)]
            )
            
            self.listener_config_sent = True
            print("✅ Dynamic listener configuration sent")
            
            return self.convert_to_protobuf_response(response)
        
        return None

    def handle_status_response(self, request):
        """Handle status responses from Envoy"""
        print("📊 Received listener status response")
        
        # For demo purposes, simulate parsing the status
        # In reality, you'd parse the actual protobuf
        print("   Simulating status parsing...")
        
        # Mark listener as ready (simulation)
        self.server.listener_status["dynamic_listener"] = "READY"
        print("   ✅ Listener status updated: dynamic_listener -> READY")

    def pack_resource(self, config: dict) -> any_pb2.Any:
        """Pack a configuration into Any protobuf"""
        # Convert dict to JSON string, then to Struct
        struct_pb = struct_pb2.Struct()
        struct_pb.update(config)
        
        # Pack into Any
        any_pb = any_pb2.Any()
        any_pb.Pack(struct_pb)
        return any_pb

    def create_empty_response(self, type_url: str):
        """Create empty response for unsupported types"""
        response = DiscoveryResponse(
            version_info="1",
            type_url=type_url,
            nonce=self.get_next_nonce(),
            resources=[]
        )
        return self.convert_to_protobuf_response(response)

    def convert_to_protobuf_response(self, response: DiscoveryResponse):
        """Convert our simple response to actual protobuf"""
        # This is a simplified conversion
        # In practice, you'd use the actual DiscoveryResponse protobuf
        import google.protobuf.message
        
        # Create a mock protobuf-like object
        class MockResponse:
            def __init__(self, data):
                self.version_info = data.version_info
                self.resources = data.resources
                self.type_url = data.type_url
                self.nonce = data.nonce
                self.control_plane = type("ControlPlane", (), {"identifier": data.control_plane_identifier})()
        
        return MockResponse(response)


def main():
    print("🚀 Simple Bidirectional xDS Management Server")
    print("=" * 50)
    print("")
    print("This is a simplified demo server that shows the core concepts")
    print("of bidirectional xDS without complex protobuf dependencies.")
    print("")
    
    # Create server components
    ads_server = SimpleBidirectionalAdsServer()
    ads_servicer = SimpleBidirectionalAdsServicer(ads_server)
    
    # Create gRPC server
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    
    # Note: This is a simplified setup. For full functionality, you'd need
    # to properly register the AggregatedDiscoveryService
    print("⚠️  This is a DEMO version - for full functionality, use:")
    print("   ./examples/reverse-xds/run_management_server.sh")
    print("")
    
    print("🎯 What this demo shows:")
    print("   • Basic ADS stream handling")
    print("   • Dynamic listener configuration")
    print("   • Reverse xDS status monitoring")
    print("   • Bidirectional communication flow")
    print("")
    
    print("🔧 To run the full demo with protobuf support:")
    print("   1. Generate protobuf files:")
    print("      ./examples/reverse-xds/generate_proto_py.sh")
    print("   2. Run full management server:")
    print("      ./examples/reverse-xds/run_management_server.sh")
    print("")
    
    # For demo purposes, just show the concepts
    print("Demo concepts illustrated - check the source code!")
    print("File: examples/reverse-xds/simple_management_server.py")


if __name__ == "__main__":
    main()