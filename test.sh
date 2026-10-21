#!/usr/bin/env bash
# Sends raw HTTP requests to the running server and checks each response.
# Usage: ./test.sh [port]    (start the server first)
# Needs openbsd-netcat (for nc -N).

PORT="${1:-8080}"
pass=0
fail=0

# Send a raw request and print the full response.
send_request() {
	printf '%s' "$1" | nc -N -w 2 localhost "$PORT"
}

# check <expected status> <description> <raw request>
check() {
	local expected="$1" desc="$2" request="$3"
	local status_line actual
	status_line=$(send_request "$request" | head -n 1 | tr -d '\r')
	actual=$(awk '{print $2}' <<<"$status_line")

	if [[ "$actual" == "$expected" ]]; then
		printf '  PASS  %s\n' "$desc"
		pass=$((pass + 1))
	else
		printf '  FAIL  %s\n        expected %s, got "%s"\n' "$desc" "$expected" "$status_line"
		fail=$((fail + 1))
	fi
}

# check_header <header line> <description> <raw request>
check_header() {
	local header="$1" desc="$2" request="$3"
	if send_request "$request" | tr -d '\r' | grep -qx "$header"; then
		printf '  PASS  %s\n' "$desc"
		pass=$((pass + 1))
	else
		printf '  FAIL  %s\n        missing header "%s"\n' "$desc" "$header"
		fail=$((fail + 1))
	fi
}

end=$'\r\n\r\n'
long_path="/$(printf 'a%.0s' {1..2100})"

echo "Valid requests"
check 200 "GET /" "GET / HTTP/1.1$end"
check 200 "GET with nested path" "GET /some/path HTTP/1.1$end"
check 200 "HTTP/1.0" "GET / HTTP/1.0$end"
check 200 "With headers" $'GET / HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n'

echo "Methods"
check 405 "POST" "POST / HTTP/1.1$end"
check 405 "DELETE" "DELETE /x HTTP/1.1$end"
check_header "Allow: GET" "405 includes Allow header" "POST / HTTP/1.1$end"
check 501 "Unknown method" "FOO / HTTP/1.1$end"
check 501 "Very long method" "ABCDEFGHIJKLMNOP / HTTP/1.1$end"
check 501 "Lowercase method" "get / HTTP/1.1$end"

echo "Malformed request lines"
check 400 "Missing version" "GET /$end"
check 400 "Method only" "GET$end"
check 400 "Extra part" "GET / HTTP/1.1 extra$end"
check 400 "Double spaces" "GET  /  HTTP/1.1$end"
check 400 "Adjacent spaces" "GET  /HTTP/1.1$end"
check 400 "Leading space" " GET / HTTP/1.1$end"
check 400 "Trailing space" "GET / HTTP/1.1 $end"
check 400 "Path without /" "GET index.html HTTP/1.1$end"

echo "Versions"
check 400 "Not HTTP" "GET / FOO/1.1$end"
check 400 "Malformed version" "GET / HTTP/x.1$end"
check 400 "Version too long" "GET / HTTP/1.11$end"
check 505 "HTTP/2.0" "GET / HTTP/2.0$end"
check 505 "HTTP/1.2" "GET / HTTP/1.2$end"

echo "Line endings"
check 400 "No line ending" "GET / HTTP/1.1"
check 400 "Bare \\r" $'GET / HTTP/1.1\rX\r\n\r\n'
check 400 "Empty request" ""

echo "Path contents"
check 400 "Control character in path" $'GET /a\x01b HTTP/1.1\r\n\r\n'
check 400 "Bare \\n in path" $'GET /a\nb HTTP/1.1\r\n\r\n'
check 400 "Escape character in path" $'GET /a\x1b[31mb HTTP/1.1\r\n\r\n'
check 400 "Non-ASCII in path" $'GET /caf\xc3\xa9 HTTP/1.1\r\n\r\n'
check 414 "Path too long" "GET $long_path HTTP/1.1$end"

echo
echo "$pass passed, $fail failed"
[[ $fail -eq 0 ]]
