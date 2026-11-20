import socket
import socketserver

BACKEND_HOST = "127.0.0.1"
BACKEND_PORT = 8080
PROXY_PORT = 8081


class TimeoutProxy(socketserver.BaseRequestHandler):
    def handle(self):
        request = self.request
        request.settimeout(10)

        data = b""

        try:
            while b"\r\n\r\n" not in data:
                chunk = request.recv(4096)
                if not chunk:
                    return
                data += chunk

            headers, body = data.split(b"\r\n\r\n", 1)

            content_length = 0
            for line in headers.split(b"\r\n"):
                if line.lower().startswith(b"content-length:"):
                    content_length = int(line.split(b":", 1)[1].strip())

            while len(body) < content_length:
                chunk = request.recv(4096)
                if not chunk:
                    return
                body += chunk

            with socket.create_connection(
                (BACKEND_HOST, BACKEND_PORT), timeout=10
            ) as backend:
                backend.sendall(headers + b"\r\n\r\n" + body[:content_length])

                # Wait for the backend response, but deliberately discard it.
                backend.settimeout(15)
                while backend.recv(4096):
                    pass

            print("Backend response received and deliberately discarded.")

        except Exception as error:
            print("Proxy error:", error)


class ThreadedServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True


if __name__ == "__main__":
    with ThreadedServer(("127.0.0.1", PROXY_PORT), TimeoutProxy) as server:
        print(f"Timeout proxy listening on http://localhost:{PROXY_PORT}")
        server.serve_forever()