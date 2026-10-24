"""Serves web/ on localhost for `make web`, telling the browser not to cache,
so a reload always picks up changed files."""

import functools
import http.server
import pathlib
import sys


class NoCache(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    root = pathlib.Path(__file__).resolve().parent
    handler = functools.partial(NoCache, directory=str(root))
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        print(f"Open http://localhost:{port} in Chrome or Edge (Ctrl-C to stop)")
        server.serve_forever()


if __name__ == "__main__":
    main()
