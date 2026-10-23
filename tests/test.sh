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

# Compare a response's status line against the expected status.
# An expected status of "none" means the server should close without replying.
verify() {
    local expected="$1" desc="$2" response="$3"
    local status_line actual
    status_line=$(head -n 1 <<<"$response" | tr -d '\r')
    actual=$(awk '{print $2}' <<<"$status_line")
    [[ -z "$actual" ]] && actual="none"

    if [[ "$actual" == "$expected" ]]; then
        printf '  PASS  %s\n' "$desc"
        pass=$((pass + 1))
    else
        printf '  FAIL  %s\n        expected %s, got "%s"\n' "$desc" "$expected" "$status_line"
        fail=$((fail + 1))
    fi
}

# check <expected status> <description> <raw request>
check() {
    verify "$1" "$2" "$(send_request "$3")"
}

# check_split <expected status> <description> <part> [<part> ...]
# Sends the request in several pieces with a pause between them,
# to test that the server keeps reading until the headers are complete.
check_split() {
    local expected="$1" desc="$2"
    shift 2
    local response
    response=$(
        for part in "$@"; do
            printf '%s' "$part"
            sleep 0.3
        done | nc -N -w 3 localhost "$PORT"
    )
    verify "$expected" "$desc" "$response"
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
check 200 "GET /"                       "GET / HTTP/1.1$end"
check 200 "GET with nested path"        "GET /some/path HTTP/1.1$end"
check 200 "HTTP/1.0"                    "GET / HTTP/1.0$end"
check 200 "With headers"                $'GET / HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n'

echo "Methods"
check 405 "POST"                        "POST / HTTP/1.1$end"
check 405 "DELETE"                      "DELETE /x HTTP/1.1$end"
check_header "Allow: GET" "405 includes Allow header" "POST / HTTP/1.1$end"
check 501 "Unknown method"              "FOO / HTTP/1.1$end"
check 501 "Very long method"            "ABCDEFGHIJKLMNOP / HTTP/1.1$end"
check 501 "Lowercase method"            "get / HTTP/1.1$end"

echo "Malformed request lines"
check 400 "Missing version"             "GET /$end"
check 400 "Method only"                 "GET$end"
check 400 "Extra part"                  "GET / HTTP/1.1 extra$end"
check 400 "Double spaces"               "GET  /  HTTP/1.1$end"
check 400 "Adjacent spaces"             "GET  /HTTP/1.1$end"
check 400 "Leading space"               " GET / HTTP/1.1$end"
check 400 "Trailing space"              "GET / HTTP/1.1 $end"
check 400 "Path without /"              "GET index.html HTTP/1.1$end"

echo "Versions"
check 400 "Not HTTP"                    "GET / FOO/1.1$end"
check 400 "Malformed version"           "GET / HTTP/x.1$end"
check 400 "Version too long"            "GET / HTTP/1.11$end"
check 505 "HTTP/2.0"                    "GET / HTTP/2.0$end"
check 505 "HTTP/1.2"                    "GET / HTTP/1.2$end"

echo "Line endings"
check 400 "Bare \\r"                    $'GET / HTTP/1.1\rX\r\n\r\n'

echo "Incomplete requests (client closes early, so no response)"
check none "Empty request"              ""
check none "No line ending"             "GET / HTTP/1.1"
check none "Headers never finished"     $'GET / HTTP/1.1\r\nHost: localhost\r\n'

echo "Path contents"
check 400 "Control character in path"  $'GET /a\x01b HTTP/1.1\r\n\r\n'
check 400 "Bare \\n in path"            $'GET /a\nb HTTP/1.1\r\n\r\n'
check 400 "Escape character in path"   $'GET /a\x1b[31mb HTTP/1.1\r\n\r\n'
check 400 "Non-ASCII in path"           $'GET /caf\xc3\xa9 HTTP/1.1\r\n\r\n'
check 414 "Path too long"               "GET $long_path HTTP/1.1$end"

echo "Reading in pieces"
check_split 200 "Split in the request line"   "GET / HT" $'TP/1.1\r\n\r\n'
check_split 200 "Split inside \\r\\n\\r\\n"   $'GET / HTTP/1.1\r\n' $'\r\n'
check_split 200 "Split between \\r and \\n"   $'GET / HTTP/1.1\r' $'\n\r\n'
check_split 200 "One byte at a time"          G E T ' ' / ' ' H T T P / 1 . 1 $'\r' $'\n' $'\r' $'\n'
check_split 405 "Split POST"                  "PO" "ST / HTTP/1.1" $'\r\n\r\n'

echo "Header size"
long_header="X-Long: $(printf 'a%.0s' {1..5000})"
check 431 "Headers larger than the buffer" "GET / HTTP/1.1"$'\r\n'"$long_header$end"
check 200 "Headers just under the limit"   "GET / HTTP/1.1"$'\r\n'"X-Fill: $(printf 'a%.0s' {1..3900})$end"

echo
echo "$pass passed, $fail failed"
[[ $fail -eq 0 ]]
