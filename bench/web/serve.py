#!/usr/bin/env python3
"""Serves the benchmark page and saves the reports it posts.

usage: serve.py DIR RESULTS_DIR [--port N]

GET serves files from DIR, uncached and cross-origin isolated, so the page's timer is as fine as the
browser allows. POST /report?label=NAME writes the body to RESULTS_DIR/web-NAME.json. It listens on
127.0.0.1 only.
"""

import argparse
import functools
import http.server
import os
import re
import urllib.parse


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".mjs": "text/javascript",
    }

    def __init__(self, *args, results, **kwargs):
        self.results = results
        super().__init__(*args, **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def do_POST(self):
        url = urllib.parse.urlparse(self.path)
        if url.path != "/report":
            self.send_error(404)
            return
        label = urllib.parse.parse_qs(url.query).get("label", ["browser"])[0]
        label = re.sub(r"[^A-Za-z0-9._-]", "-", label)[:64] or "browser"
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        with open(os.path.join(self.results, f"web-{label}.json"), "wb") as f:
            f.write(body)
        self.send_response(204)
        self.end_headers()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dir")
    parser.add_argument("results")
    parser.add_argument("--port", type=int, default=8419)
    args = parser.parse_args()
    os.makedirs(args.results, exist_ok=True)
    handler = functools.partial(Handler, directory=args.dir, results=os.path.abspath(args.results))
    with http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
        server.serve_forever()


if __name__ == "__main__":
    main()
