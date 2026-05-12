#!/usr/bin/env python3
import os
import sys

content_length = int(os.environ.get('CONTENT_LENGTH', 0))

print("Content-Type: text/plain")
print("")

if content_length > 0:
    body = sys.stdin.read(content_length)
    print("Received body:")
    print(body)
else:
    print("No body received")
