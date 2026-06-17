const { SerialPort } = require('serialport');
const http = require('http');
const fs = require('fs');
const path = require('path');

let buffer = '';
let latestResponse = '';

const port = new SerialPort({
    path: 'COM3',
    baudRate: 115200,
});

port.on('open', () => {
    console.log('port on');
});

port.on('data', (data) => {
    buffer += data.toString();
    let start = buffer.indexOf('|');

    while (start !== -1) {
        const end = buffer.indexOf(';', start);
        if (end === -1) break;

        const message = buffer.slice(start, end + 1);
        buffer = buffer.slice(end + 1);
        latestResponse = message;
        console.log('получено:', message);

        start = buffer.indexOf('|');
    }
});

function sendCommand(name, num, cmd) {
    const command = '|' + name + num + ':' + cmd + ';';
    port.write(command);
    console.log('отправлено:', command);
}

const server = http.createServer((req, res) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
    }

    if (req.method === 'POST' && req.url === '/send') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const { name, num, cmd } = JSON.parse(body);
                sendCommand(name, num, cmd);
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ status: 'sent' }));
            } catch (e) {
                res.writeHead(400);
                res.end('bad request');
            }
        });
        return;
    }

    if (req.method === 'GET' && req.url === '/response') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ response: latestResponse }));
        return;
    }

    if (req.method === 'GET' && (req.url === '/' || req.url === '/index.html')) {
        const indexPath = path.join(__dirname, 'index.html');
        fs.readFile(indexPath, 'utf8', (err, data) => {
            if (err) {
                res.writeHead(500, { 'Content-Type': 'text/plain' });
                res.end('Server error');
                return;
            }
            res.writeHead(200, { 'Content-Type': 'text/html' });
            res.end(data);
        });
        return;
    }

    res.writeHead(404);
    res.end();
});

server.listen(3000, () => {
    console.log('http://localhost:3000');
});