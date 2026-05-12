#!/usr/bin/env python3
import os

print("Content-Type: text/html")
print("")
print("<html><body>")
print("<h1>CGI Test</h1>")
print("<p>This script was executed by webserv!</p>")
print("<h2>Environment Variables</h2>")
print("<ul>")
for key in sorted(os.environ.keys()):
    print(f"<li><strong>{key}:</strong> {os.environ[key]}</li>")
print("</ul>")
print("</body></html>")
