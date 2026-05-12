<?php
header("Content-Type: text/html");
echo "<!DOCTYPE html>\n";
echo "<html><head><title>PHP CGI Info</title></head>\n";
echo "<body>\n";
echo "<h1>PHP CGI Info</h1>\n";
echo "<p>This is served by PHP-CGI!</p>\n";
echo "<table border='1'>\n";
foreach ($_SERVER as $key => $value) {
    if (strpos($key, 'HTTP_') === 0 || in_array($key, array('REQUEST_METHOD', 'QUERY_STRING', 'CONTENT_TYPE', 'CONTENT_LENGTH', 'SCRIPT_NAME', 'SERVER_PORT'))) {
        echo "<tr><td>" . htmlspecialchars($key) . "</td><td>" . htmlspecialchars($value) . "</td></tr>\n";
    }
}
echo "</table>\n";
echo "</body></html>\n";
?>