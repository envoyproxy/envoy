#!/usr/bin/env python3

import datetime
import http.server
import json
import os
import socketserver
import subprocess
import sys
import time


class MockHandler(http.server.SimpleHTTPRequestHandler):

    def do_GET(self):
        if self.path == '/ping':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Server', 'EnvoyMockServer/1.0')
            self.send_header('X-Envoy-Upstream-Service-Time', '1')
            self.end_headers()
            response = {
                'status': 'ok',
                'timestamp': datetime.datetime.now(datetime.UTC).isoformat()
            }
            self.wfile.write(json.dumps(response).encode())
            print(f"[{datetime.datetime.now(datetime.UTC).isoformat()}] 200 GET /ping")
        else:
            self.send_response(404)
            self.end_headers()
            print(f"[{datetime.datetime.now(datetime.UTC).isoformat()}] 404 GET {self.path}")

    def log_message(self, format, *args):
        pass


def start_server(port):
    """Start the mock server"""
    try:
        with socketserver.TCPServer(("127.0.0.1", port), MockHandler) as httpd:
            print(f"Mock server ready on 127.0.0.1:{port}")
            httpd.serve_forever()
    except OSError as e:
        print(f"Failed to start server on port {port}: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}")
        sys.exit(1)


def verify_server(port, max_retries=3):
    """Verify the server is running"""
    for i in range(max_retries):
        try:
            result = subprocess.run(["curl", "-s", f"http://127.0.0.1:{port}/ping"],
                                    capture_output=True,
                                    text=True,
                                    timeout=5)
            if result.returncode == 0 and "ok" in result.stdout:
                return True
        except subprocess.TimeoutExpired:
            pass
        except Exception:
            pass
        if i < max_retries - 1:
            time.sleep(1)
    return False


def kill_server(pid):
    try:
        os.kill(pid, 9)
    except:
        pass
    sys.exit(1)


def fork_server():
    devnull = os.open(os.devnull, os.O_RDWR)
    os.dup2(devnull, 0)  # stdin
    os.dup2(devnull, 1)  # stdout
    os.dup2(devnull, 2)  # stderr
    os.close(devnull)
    os.setsid()


def setup_server_logging():
    log_file = open('/tmp/mock_server.log', 'a')
    sys.stdout = log_file
    sys.stderr = log_file


def main():
    port = int(os.environ.get("MOCK_SERVER_PORT", "10000"))
    print(f"Starting mock HTTP server on port {port}...")
    sys.stdout.flush()  # Ensure output is flushed before fork

    pid = os.fork()
    if pid > 0:
        # Parent process
        time.sleep(2)
        if verify_server(port):
            print(f"✅ Mock server is running on port {port} (PID: {pid})")
            print(pid)
            sys.stdout.flush()  # Ensure all output is flushed
        else:
            print("❌ Failed to start mock server")
            kill_server(pid)
        return

    # Child process - immediately detach
    fork_server()
    setup_server_logging()
    start_server(port)


if __name__ == "__main__":
    main()
