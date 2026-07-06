import http.server
import socketserver

class CORSRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # Dev server: never let the browser cache. Without cache headers,
        # Chrome caches heuristically and will serve a STALE .wasm/.js across
        # rebuilds - "fixed" bugs keep reproducing until a manual hard refresh.
        self.send_header("Cache-Control", "no-cache, must-revalidate")
        super().end_headers()

PORT = 8081
with socketserver.TCPServer(("", PORT), CORSRequestHandler) as httpd:
    print(f"Serving at http://localhost:{PORT}")
    httpd.serve_forever()
