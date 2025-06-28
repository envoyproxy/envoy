#!/usr/bin/env python3

import http.server
import socketserver
import json
from datetime import datetime

class BackendHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        # Create a response showing that the backend service is working
        response = {
            "message": "Hello from on-premises backend service!",
            "timestamp": datetime.now().isoformat(),
            "path": self.path,
            "method": "GET"
        }
        
        self.send_response(200)
        self.send_header('Content-type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps(response, indent=2).encode())

    def do_POST(self):
        # Handle POST requests as well
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8') if content_length > 0 else ""
        
        response = {
            "message": "POST request received by on-premises backend service!",
            "timestamp": datetime.now().isoformat(),
            "path": self.path,
            "method": "POST",
            "body": body
        }
        
        self.send_response(200)
        self.send_header('Content-type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps(response, indent=2).encode())

if __name__ == "__main__":
    PORT = 7070
    with socketserver.TCPServer(("", PORT), BackendHandler) as httpd:
        print(f"Backend service running on port {PORT}")
        print(f"Visit http://localhost:{PORT}/on_prem_service to test")
        httpd.serve_forever() 