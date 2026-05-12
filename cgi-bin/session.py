#!/usr/bin/env python3
import os
import cgi
import cgitb
import html

cgitb.enable()

print("Content-Type: text/html")
print()

form = cgi.FieldStorage()
username = form.getvalue("username", "")
password = form.getvalue("password", "")
action = form.getvalue("action", "")

if action == "logout":
    print("<html><body>")
    print("<h1>Logged Out</h1>")
    print("<p>You have been logged out.</p>")
    print('<p><a href="/login.html">Login again</a></p>')
    print("</body></html>")
elif username:
    print("<html><body>")
    print(f"<h1>Welcome, {html.escape(username)}!</h1>")
    print("<p>You are now logged in.</p>")
    print('<p><a href="/welcome.html">Go to welcome page</a></p>')
    print("</body></html>")
else:
    print("<html><body>")
    print("<h1>Login Required</h1>")
    print('<p><a href="/login.html">Go to login page</a></p>')
    print("</body></html>")