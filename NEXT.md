- log the parsed parts for successful requests, like method=GET path=/ version=HTTP/1.1, method_names[method] gives us the method string.
- create http_request wrapper struct (wraps http_request_line and more)
- split into multiple files

- [x] Setup: C compiler, a Makefile, a git repo with NEXT.md. Skim the stream-server section of Beej's guide.
- [x] Milestone 1, hello world: accept a connection, print the raw request, send a hardcoded 200 OK. Test with curl -v and a browser.
[ ] Milestone 2, request line: parse method, path and version. Return 400 for garbage and 405 for non-GET.
[ ] Milestone 3, read properly: loop on recv until \r\n\r\n, cap the header size, reject oversized requests.
[ ] Milestone 4, serve files: map paths to a folder, 404 for missing files, Content-Type and Content-Length, block ../.
[ ] Milestone 5, polish: HEAD, Connection: close, request logging.
[ ] Milestone 6, concurrency (stretch): fork() per connection, then threads. poll is a bonus.
