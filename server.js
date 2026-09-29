const { SerialPort } = require('serialport');
const http = require('http');
const fs = require('fs');
const path = require('path');

// ========== SERIAL ==========

let buffer = '';
let latestResponse = '';
const trackData = {
    path: [],
    measurements: [],
    currentPosition: null,
};
const MAX_TRACK_POINTS = 10000;

const port = new SerialPort({
    path: 'COM5',
    baudRate: 115200,
});

port.on('open', () => {
    console.log('порт открыт');
    // ждём 2 секунды пока ардуино загрузится, потом стартуем опрос
    setTimeout(startPoll, 2000);
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

        // парсим сообщение и пропускаем через автопол
        const match = message.match(/\|([TPGD])([01]):([MR\d]+);/);
        if (match) {
            const [, name, num, value] = match;
            handleAutoPoll(name, num, value);
        }

        start = buffer.indexOf('|');
    }
});

function sendCommand(name, num, cmd) {
    const command = '|' + name + num + ':' + cmd + ';';
    port.write(command);
    console.log('отправлено:', command);
}

// ========== АВТООПРОС ==========

const pollQueue = [
    { name: 'T', num: '0', maxWait: 12000 },
    { name: 'T', num: '1', maxWait: 12000 },
    { name: 'P', num: '0', maxWait: 7000 },
    { name: 'P', num: '1', maxWait: 7000 },
    { name: 'G', num: '0', maxWait: 3000 },
    { name: 'D', num: '0', maxWait: 12000 },
    { name: 'D', num: '1', maxWait: 12000 },
];

let pollIndex = 0;
let pollTimer = null;
let pollActive = false;

function startPoll() {
    pollIndex = 0;
    pollActive = true;
    pollNext();
}

function stopPoll() {
    pollActive = false;
    clearTimeout(pollTimer);
}

function pollNext() {
    if (!pollActive) return;
    if (pollIndex >= pollQueue.length) {
        pollIndex = 0;
    }

    const s = pollQueue[pollIndex];
    sendCommand(s.name, s.num, 'M');

    // страховка: если датчик молчит — идём дальше
    pollTimer = setTimeout(() => {
        console.log('таймаут датчика:', s.name + s.num);
        pollIndex++;
        pollNext();
    }, s.maxWait);
}

function handleAutoPoll(sensorName, sensorNum, value) {
    if (!pollActive) return;

    const current = pollQueue[pollIndex];
    if (current.name !== sensorName || current.num !== sensorNum) {
        return;
    }

    if (value === 'M') {
        // датчик занят, спросим позже
        clearTimeout(pollTimer);

        pollTimer = setTimeout(() => {
            sendCommand(sensorName, sensorNum, 'R');

            // страховка на R
            pollTimer = setTimeout(() => {
                console.log('таймаут на R:', sensorName + sensorNum);
                pollIndex++;
                pollNext();
            }, current.maxWait);
        }, 500);

    } else {
        // получили число
        clearTimeout(pollTimer);
        console.log('результат:', sensorName + sensorNum, '=', value);

        pollIndex++;
        setTimeout(pollNext, 100);
    }
}

// ========== HTTP СЕРВЕР ==========

function readJsonBody(req, callback) {
    let body = '';

    req.on('data', chunk => {
        body += chunk;
        if (body.length > 1024 * 1024) req.destroy();
    });
    req.on('end', () => {
        try {
            callback(null, JSON.parse(body));
        } catch (error) {
            callback(error);
        }
    });
}

function serveStaticFile(res, root, relativePath) {
    let decodedPath;

    try {
        decodedPath = decodeURIComponent(relativePath);
    } catch (error) {
        res.writeHead(400);
        res.end('bad request');
        return;
    }

    const filePath = path.resolve(root, decodedPath);
    const relative = path.relative(root, filePath);
    if (relative.startsWith('..') || path.isAbsolute(relative)) {
        res.writeHead(403);
        res.end('forbidden');
        return;
    }

    const contentTypes = {
        '.css': 'text/css; charset=utf-8',
        '.js': 'application/javascript; charset=utf-8',
        '.png': 'image/png',
    };

    fs.readFile(filePath, (error, data) => {
        if (error) {
            res.writeHead(error.code === 'ENOENT' ? 404 : 500);
            res.end();
            return;
        }

        const contentType = contentTypes[path.extname(filePath).toLowerCase()] ||
            'application/octet-stream';
        res.writeHead(200, {
            'Content-Type': contentType,
            'Cache-Control': 'public, max-age=86400',
        });
        res.end(data);
    });
}

const server = http.createServer((req, res) => {
    const requestUrl = new URL(req.url, 'http://localhost');
    const pathname = requestUrl.pathname;

    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
    }

    // API: отправить команду вручную
    if (req.method === 'POST' && pathname === '/send') {
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

    // API: получить последний ответ
    if (req.method === 'GET' && pathname === '/response') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ response: latestResponse }));
        return;
    }

    // API: принять новую GPS-точку и необязательный результат замера
    if (req.method === 'POST' && pathname === '/track') {
        readJsonBody(req, (error, data) => {
            const lat = Number(data?.lat);
            const lon = Number(data?.lon);

            if (error || !Number.isFinite(lat) || !Number.isFinite(lon) ||
                lat < -90 || lat > 90 || lon < -180 || lon > 180) {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: 'invalid GPS point' }));
                return;
            }

            const point = {
                lat,
                lon,
                recordedAt: data.recordedAt || new Date().toISOString(),
            };
            trackData.path.push(point);
            trackData.currentPosition = point;

            if (trackData.path.length > MAX_TRACK_POINTS) {
                trackData.path.splice(0, trackData.path.length - MAX_TRACK_POINTS);
            }

            if (data.measurement && typeof data.measurement === 'object') {
                trackData.measurements.push({
                    id: data.measurement.id || 'M-' + trackData.measurements.length,
                    lat,
                    lon,
                    status: data.measurement.status === 'ok' ? 'ok' : 'fail',
                    measuredAt: data.measurement.measuredAt || point.recordedAt,
                    values: data.measurement.values || {},
                });
            }

            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ status: 'saved' }));
        });
        return;
    }

    // API: получить пройденный маршрут и точки замеров
    if (req.method === 'GET' && pathname === '/track') {
        res.writeHead(200, {
            'Content-Type': 'application/json',
            'Cache-Control': 'no-store',
        });
        res.end(JSON.stringify(trackData));
        return;
    }

    // Локальные файлы Leaflet и офлайн-тайлы карты
    if (req.method === 'GET' && pathname.startsWith('/vendor/leaflet/')) {
        serveStaticFile(
            res,
            path.join(__dirname, 'vendor', 'leaflet'),
            pathname.slice('/vendor/leaflet/'.length)
        );
        return;
    }

    if (req.method === 'GET' && pathname.startsWith('/tiles/')) {
        serveStaticFile(
            res,
            path.join(__dirname, 'tiles'),
            pathname.slice('/tiles/'.length)
        );
        return;
    }

    // раздача фронта
    if (req.method === 'GET' && (pathname === '/' || pathname === '/index.html')) {
        const indexPath = path.join(__dirname, 'index.html');
        fs.readFile(indexPath, 'utf8', (err, data) => {
            if (err) {
                res.writeHead(500, { 'Content-Type': 'text/plain' });
                res.end('server error');
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
