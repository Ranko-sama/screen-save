// Development only: expose this one test page on loopback, never the filesystem.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const page = fs.readFileSync(path.join(__dirname, 'bench.html'));
http.createServer((request, response) => {
  if (request.method === 'POST' && request.url === '/result') {
    let body = '';
    request.on('data', chunk => body += chunk);
    request.on('end', () => {
      fs.appendFileSync(path.join(__dirname, 'artifacts', 'gamma-browser-complete.jsonl'), body + '\n');
      response.writeHead(204); response.end();
    });
    return;
  }
  if (request.url !== '/') { response.writeHead(404); response.end(); return; }
  response.writeHead(200, {'Content-Type':'text/html; charset=utf-8', 'Cache-Control':'no-store'});
  response.end(page);
}).listen(18741, '127.0.0.1', () => console.log('Test page: http://127.0.0.1:18741/'));
