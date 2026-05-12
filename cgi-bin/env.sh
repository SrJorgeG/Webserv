#!/bin/bash
echo "Content-Type: text/html"
echo ""
echo "<!DOCTYPE html>"
echo "<html><head><title>Shell CGI</title></head>"
echo "<body>"
echo "<h1>Shell CGI Environment</h1>"
echo "<p>This is served by a shell script!</p>"
echo "<table border='1'>"
for var in REQUEST_METHOD QUERY_STRING CONTENT_TYPE CONTENT_LENGTH SCRIPT_NAME SERVER_PORT HTTP_HOST HTTP_USER_AGENT HTTP_ACCEPT; do
    echo "<tr><td>$var</td><td>${!var}</td></tr>"
done
echo "</table>"
echo "</body></html>"