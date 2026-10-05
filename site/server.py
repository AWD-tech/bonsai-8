import http.server, os
os.chdir(os.path.join(os.path.dirname(__file__), 'public'))
http.server.ThreadingHTTPServer(('0.0.0.0', int(os.environ.get('PORT', '5173'))), http.server.SimpleHTTPRequestHandler).serve_forever()
