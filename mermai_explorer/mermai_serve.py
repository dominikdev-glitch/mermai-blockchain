#!/usr/bin/env python3
"""
MERMAI Web Explorer HTTP Server & Reverse Proxy
Serves static dashboard files and proxies JSON-RPC requests with CORS headers.
"""


try:
    if sys.platform == "win32":
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass
import http.server
import socketserver
import urllib.request
import urllib.parse
import os
import sys

PORT = 8080
DIRECTORY = os.path.dirname(os.path.abspath(__file__))

class ExplorerHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def do_POST(self):
        # Proxy JSON-RPC requests to local C++ node ports
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/rpc":
            qs = urllib.parse.parse_qs(parsed.query)
            target_port = qs.get("port", ["6334"])[0]
            content_len = int(self.headers.get('Content-Length', 0))
            post_body = self.rfile.read(content_len)

            try:
                req = urllib.request.Request(
                    f"http://127.0.0.1:{target_port}",
                    data=post_body,
                    headers={"Content-Type": "application/json"}
                )
                with urllib.request.urlopen(req, timeout=3) as resp:
                    resp_data = resp.read()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    self.wfile.write(resp_data)
            except Exception as e:
                self.send_response(502)
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                self.wfile.write(b'{"error": "Node unreachable"}')
        else:
            self.send_response(404)
            self.end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

def main():
    port = PORT
    if "--port" in sys.argv:
        idx = sys.argv.index("--port")
        if idx + 1 < len(sys.argv):
            port = int(sys.argv[idx + 1])

    print("=" * 60)
    print(f"  MERMAI BLOCKCHAIN WEB EXPLORER & DASHBOARD")
    print("=" * 60)
    print(f"  [RUN] Serving Explorer on http://127.0.0.1:{port}")
    print(f"  ? Proxying RPC to Mermai Nodes on ports 6334, 6336, 6338")
    print(f"  Open http://localhost:{port} in your browser!")
    print("=" * 60 + "\n")

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", port), ExplorerHandler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[STOP] Explorer server stopped.")

if __name__ == "__main__":
    main()
