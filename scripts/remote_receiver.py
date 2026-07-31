#!/usr/bin/env python3
"""Minimal LAN HTTP receiver for audio_event remote-report demonstrations."""

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


REQUIRED_FIELDS = {
    "schema_version",
    "event_id",
    "device_id",
    "event",
    "confidence_permille",
    "monotonic_ms",
}


class AudioEventHandler(BaseHTTPRequestHandler):
    seen_event_ids = set()

    def do_POST(self):
        if self.path != "/api/v1/audio-events":
            self.send_error(404, "unknown endpoint")
            return

        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error(400, "invalid Content-Length")
            return

        if content_length <= 0 or content_length > 4096:
            self.send_error(413, "payload size must be 1..4096 bytes")
            return

        try:
            event = json.loads(self.rfile.read(content_length))
        except json.JSONDecodeError:
            self.send_error(400, "invalid JSON")
            return

        missing = REQUIRED_FIELDS.difference(event)
        if missing:
            self.send_error(400, "missing fields: " + ", ".join(sorted(missing)))
            return

        event_id = event["event_id"]
        duplicate = event_id in self.seen_event_ids
        self.seen_event_ids.add(event_id)
        print(json.dumps({"duplicate": duplicate, "event": event}, ensure_ascii=False),
              flush=True)

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"accepted":true}\n')

    def log_message(self, fmt, *args):
        print("[http] " + fmt % args, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), AudioEventHandler)
    print(f"listening on http://{args.host}:{args.port}/api/v1/audio-events")
    server.serve_forever()


if __name__ == "__main__":
    main()
