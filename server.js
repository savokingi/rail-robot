const { EventEmitter } = require('events');
const { SerialPort } = require('serialport');
const http = require('http');
const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const HTTP_PORT = Number(process.env.HTTP_PORT || 3000);
const SERIAL_BAUD = Number(process.env.SERIAL_BAUD || 115200);
const MEASURE_EVERY_METERS = Number(process.env.MEASURE_EVERY_METERS || 5);
const MAX_RAIL_OFFSET_METERS = Number(process.env.MAX_RAIL_OFFSET_METERS || 50);
const INTERNAL_EMULATOR = process.argv.includes('--emulator') || process.env.ESP_MODE === 'emulator';
const CLI_PORT = process.argv.find(arg => arg.startsWith('--port='))?.slice(7);
const SESSIONS_PATH = path.join(ROOT, 'data', 'sessions.json');
const RAILWAY_PATH = path.join(ROOT, 'data', 'railway-line.geojson');

const railwayGeoJson = JSON.parse(fs.readFileSync(RAILWAY_PATH, 'utf8'));
const railwayFeature = railwayGeoJson.features[0];
const railwayCoordinates = railwayFeature.geometry.coordinates.map(([lon, lat]) => ({ lat, lon }));
const railwayName = railwayFeature.properties.name;

function haversineMeters(a, b) {
    const radius = 6371000;
    const toRad = value => value * Math.PI / 180;
    const dLat = toRad(b.lat - a.lat);
    const dLon = toRad(b.lon - a.lon);
    const value = Math.sin(dLat / 2) ** 2 +
        Math.cos(toRad(a.lat)) * Math.cos(toRad(b.lat)) * Math.sin(dLon / 2) ** 2;
    return 2 * radius * Math.atan2(Math.sqrt(value), Math.sqrt(1 - value));
}

const railwaySegments = [];
let railwayLengthMeters = 0;
for (let index = 0; index < railwayCoordinates.length - 1; index++) {
    const start = railwayCoordinates[index];
    const end = railwayCoordinates[index + 1];
    const length = haversineMeters(start, end);
    railwaySegments.push({ start, end, length, offset: railwayLengthMeters });
    railwayLengthMeters += length;
}

function formatRailKm(meters) {
    const safeMeters = Math.max(0, Math.round(meters));
    return `${Math.floor(safeMeters / 1000)}+${String(safeMeters % 1000).padStart(3, '0')}`;
}

function snapToRail(lat, lon) {
    const point = { lat: Number(lat), lon: Number(lon) };
    const metersPerLat = 111320;
    const metersPerLon = 111320 * Math.cos(point.lat * Math.PI / 180);
    let best = null;

    for (const segment of railwaySegments) {
        const ax = (segment.start.lon - point.lon) * metersPerLon;
        const ay = (segment.start.lat - point.lat) * metersPerLat;
        const bx = (segment.end.lon - point.lon) * metersPerLon;
        const by = (segment.end.lat - point.lat) * metersPerLat;
        const dx = bx - ax;
        const dy = by - ay;
        const lengthSquared = dx * dx + dy * dy;
        const ratio = lengthSquared === 0 ? 0 : Math.max(0, Math.min(1, -(ax * dx + ay * dy) / lengthSquared));
        const x = ax + dx * ratio;
        const y = ay + dy * ratio;
        const distance = Math.sqrt(x * x + y * y);

        if (!best || distance < best.distanceToRailMeters) {
            const railDistanceMeters = segment.offset + segment.length * ratio;
            best = {
                lat: segment.start.lat + (segment.end.lat - segment.start.lat) * ratio,
                lon: segment.start.lon + (segment.end.lon - segment.start.lon) * ratio,
                railDistanceMeters,
                railKm: formatRailKm(railDistanceMeters),
                distanceToRailMeters: distance,
                onRail: distance <= MAX_RAIL_OFFSET_METERS,
            };
        }
    }
    return best;
}

function interpolateRail(distanceMeters) {
    const target = Math.max(0, Math.min(railwayLengthMeters, distanceMeters));
    const segment = railwaySegments.find(item => target <= item.offset + item.length) || railwaySegments.at(-1);
    const ratio = segment.length ? (target - segment.offset) / segment.length : 0;
    return {
        lat: segment.start.lat + (segment.end.lat - segment.start.lat) * ratio,
        lon: segment.start.lon + (segment.end.lon - segment.start.lon) * ratio,
    };
}

class EspEmulator extends EventEmitter {
    constructor() {
        super();
        this.path = 'internal://esp-emulator';
        this.isOpen = true;
        this.direction = 'stop';
        this.distanceMeters = 0;
        this.wheelTicks = 0;
        this.lastHeartbeatAt = Date.now();
        this.lastTickAt = Date.now();
        this.lastTelemetryAt = 0;
        this.timer = setInterval(() => this.tick(), 100);
        setImmediate(() => this.emit('open'));
    }

    write(raw, callback) {
        for (const line of String(raw).split('\n').filter(Boolean)) {
            try { this.handleCommand(JSON.parse(line)); }
            catch (_) { this.send({ type: 'error', message: 'invalid JSON command' }); }
        }
        if (callback) callback(null);
    }

    send(message) {
        this.emit('data', Buffer.from(JSON.stringify(message) + '\n'));
    }

    acknowledge(message, extra = {}) {
        if (message.id !== undefined) this.send({ type: 'ack', id: message.id, ok: true, ...extra });
    }

    handleCommand(message) {
        const command = message.command;
        if (command === 'heartbeat') {
            this.lastHeartbeatAt = Date.now();
            this.direction = message.direction || this.direction;
        } else if (command === 'reset') {
            this.distanceMeters = 0;
            this.wheelTicks = 0;
            this.direction = 'stop';
            this.acknowledge(message);
        } else if (command === 'move') {
            this.direction = message.direction === 'back' ? 'back' : 'forward';
            this.lastHeartbeatAt = Date.now();
            this.acknowledge(message);
        } else if (command === 'stop') {
            this.direction = 'stop';
            this.acknowledge(message);
        } else if (command === 'measure') {
            this.acknowledge(message);
            setTimeout(() => {
                const defect = Math.random() < 0.2;
                this.send({
                    type: 'measurement',
                    measurementId: message.measurementId,
                    hardness: defect ? 790 + Math.round(Math.random() * 70) : 590 + Math.round(Math.random() * 100),
                    status: defect ? 'defect' : 'ok',
                });
            }, 900);
        } else if (command === 'mark') {
            this.acknowledge(message);
            setTimeout(() => this.send({ type: 'marked', measurementId: message.measurementId }), 500);
        } else {
            this.send({ type: 'ack', id: message.id, ok: false, error: 'unknown command' });
        }
    }

    tick() {
        const now = Date.now();
        const elapsed = Math.min(0.5, (now - this.lastTickAt) / 1000);
        this.lastTickAt = now;
        if (this.direction !== 'stop' && now - this.lastHeartbeatAt > 1200) this.direction = 'stop';
        const sign = this.direction === 'forward' ? 1 : this.direction === 'back' ? -1 : 0;
        const speed = sign * 1.25;
        this.distanceMeters = Math.max(0, Math.min(railwayLengthMeters, this.distanceMeters + speed * elapsed));
        this.wheelTicks = Math.round(this.distanceMeters * 20);

        if (now - this.lastTelemetryAt >= 400) {
            this.lastTelemetryAt = now;
            const gps = interpolateRail(this.distanceMeters);
            this.send({
                type: 'telemetry', lat: gps.lat, lon: gps.lon, speed: Math.abs(speed),
                distanceMeters: this.distanceMeters, wheelTicks: this.wheelTicks, motor: this.direction,
            });
        }
    }

    close() {
        clearInterval(this.timer);
        this.isOpen = false;
        this.emit('close');
    }
}

let transport = null;
let serialBuffer = '';
let reconnectTimer = null;
let nextCommandId = 1;
let cycleBusy = false;
let manualDirection = null;
let pendingMeasurement = null;
const pendingCommands = new Map();

const robot = {
    phase: 'disconnected',
    connection: { connected: false, mode: INTERNAL_EMULATOR ? 'emulator' : 'serial', port: null, error: null },
    telemetry: null,
    latestMeasurement: null,
    session: null,
    config: {
        measureEveryMeters: MEASURE_EVERY_METERS,
        maxRailOffsetMeters: MAX_RAIL_OFFSET_METERS,
        railwayName,
        railwayLengthMeters: Math.round(railwayLengthMeters),
    },
};

function addEvent(kind, message) {
    if (!robot.session) return;
    robot.session.events.push({ at: new Date().toISOString(), kind, message });
    if (robot.session.events.length > 500) robot.session.events.shift();
}

function loadSessions() {
    try { return JSON.parse(fs.readFileSync(SESSIONS_PATH, 'utf8')); }
    catch (_) { return []; }
}

let storedSessions = loadSessions();
let persistTimer = null;
function persistSessions() {
    clearTimeout(persistTimer);
    persistTimer = setTimeout(() => {
        const sessions = [...storedSessions];
        if (robot.session && !sessions.some(item => item.id === robot.session.id)) sessions.push(robot.session);
        fs.writeFile(SESSIONS_PATH, JSON.stringify(sessions, null, 2), error => {
            if (error) console.error('Не удалось сохранить рейсы:', error.message);
        });
    }, 200);
}

async function chooseSerialPort() {
    if (CLI_PORT || process.env.SERIAL_PORT) return CLI_PORT || process.env.SERIAL_PORT;
    const ports = await SerialPort.list();
    const likely = ports.find(port => /ESP|CP210|CH340|USB|Arduino/i.test(
        `${port.manufacturer || ''} ${port.friendlyName || ''} ${port.pnpId || ''}`
    ));
    return likely?.path || (ports.length === 1 ? ports[0].path : null);
}

function attachTransport(device) {
    transport = device;
    serialBuffer = '';
    robot.connection.connected = Boolean(device.isOpen);
    robot.connection.port = device.path;
    robot.connection.error = null;
    robot.phase = 'idle';
    device.on('open', () => {
        robot.connection.connected = true;
        robot.connection.port = device.path;
        robot.connection.error = null;
        if (robot.phase === 'disconnected') robot.phase = 'idle';
        console.log('ESP подключена:', device.path);
    });
    device.on('data', data => processSerialData(data.toString()));
    device.on('error', error => {
        robot.connection.error = error.message;
        console.error('Ошибка ESP:', error.message);
    });
    device.on('close', () => {
        robot.connection.connected = false;
        robot.phase = 'disconnected';
        transport = null;
        scheduleReconnect();
    });
}

async function connectEsp() {
    if (transport) return;
    try {
        if (INTERNAL_EMULATOR) return attachTransport(new EspEmulator());
        const serialPath = await chooseSerialPort();
        if (!serialPath) throw new Error('ESP не найдена. Укажите --port=COM5 или SERIAL_PORT');
        attachTransport(new SerialPort({ path: serialPath, baudRate: SERIAL_BAUD }));
    } catch (error) {
        robot.connection.connected = false;
        robot.connection.error = error.message;
        robot.phase = 'disconnected';
        console.error(error.message);
        scheduleReconnect();
    }
}

function scheduleReconnect() {
    if (INTERNAL_EMULATOR || reconnectTimer) return;
    reconnectTimer = setTimeout(() => { reconnectTimer = null; connectEsp(); }, 3000);
}

function processSerialData(chunk) {
    serialBuffer += chunk;
    let newline = serialBuffer.indexOf('\n');
    while (newline !== -1) {
        const line = serialBuffer.slice(0, newline).trim();
        serialBuffer = serialBuffer.slice(newline + 1);
        if (line) {
            try { handleEspMessage(JSON.parse(line)); }
            catch (_) { console.error('Некорректный ответ ESP:', line); }
        }
        newline = serialBuffer.indexOf('\n');
    }
}

function handleEspMessage(message) {
    if (message.type === 'ack') {
        const pending = pendingCommands.get(message.id);
        if (pending) {
            clearTimeout(pending.timer);
            pendingCommands.delete(message.id);
            message.ok ? pending.resolve(message) : pending.reject(new Error(message.error || 'ESP rejected command'));
        }
    } else if (message.type === 'telemetry') {
        handleTelemetry(message);
    } else if (message.type === 'measurement') {
        robot.latestMeasurement = message;
        if (pendingMeasurement) {
            clearTimeout(pendingMeasurement.timer);
            const resolve = pendingMeasurement.resolve;
            pendingMeasurement = null;
            resolve(message);
        }
    } else if (message.type === 'marked') {
        addEvent('mark', 'Дефект отмечен краской');
    }
}

function sendRaw(message) {
    if (!transport || !robot.connection.connected) throw new Error('ESP не подключена');
    transport.write(JSON.stringify(message) + '\n', error => {
        if (error) console.error('Ошибка записи ESP:', error.message);
    });
}

function sendCommand(command, payload = {}, timeoutMs = 2500) {
    const id = nextCommandId++;
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            pendingCommands.delete(id);
            reject(new Error(`ESP не подтвердила команду ${command}`));
        }, timeoutMs);
        pendingCommands.set(id, { resolve, reject, timer });
        try { sendRaw({ type: 'command', id, command, ...payload }); }
        catch (error) { clearTimeout(timer); pendingCommands.delete(id); reject(error); }
    });
}

function waitForMeasurement(timeoutMs = 6000) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            pendingMeasurement = null;
            reject(new Error('Твердомер не вернул результат'));
        }, timeoutMs);
        pendingMeasurement = { resolve, reject, timer };
    });
}

function handleTelemetry(message) {
    const snap = snapToRail(message.lat, message.lon);
    robot.telemetry = {
        receivedAt: new Date().toISOString(), rawLat: Number(message.lat), rawLon: Number(message.lon),
        lat: snap.lat, lon: snap.lon, railDistanceMeters: snap.railDistanceMeters, railKm: snap.railKm,
        distanceToRailMeters: Math.round(snap.distanceToRailMeters * 10) / 10, onRail: snap.onRail,
        speed: Number(message.speed || 0), distanceMeters: Number(message.distanceMeters || 0),
        wheelTicks: Number(message.wheelTicks || 0), motor: message.motor || 'stop',
    };

    const session = robot.session;
    if (!session?.active) return;
    session.distanceMeters = robot.telemetry.distanceMeters;
    session.currentRailKm = snap.railKm;
    const previous = session.path.at(-1);
    if (!previous || Math.abs(robot.telemetry.distanceMeters - previous.distanceMeters) >= 0.5) {
        session.path.push({
            lat: snap.lat, lon: snap.lon, rawLat: Number(message.lat), rawLon: Number(message.lon),
            railKm: snap.railKm, railDistanceMeters: snap.railDistanceMeters,
            distanceToRailMeters: snap.distanceToRailMeters, distanceMeters: robot.telemetry.distanceMeters,
            recordedAt: robot.telemetry.receivedAt,
        });
    }
    if (robot.phase === 'running' && !cycleBusy &&
        robot.telemetry.distanceMeters - session.lastMeasurementDistance >= MEASURE_EVERY_METERS) {
        runMeasurementCycle();
    }
}

async function runMeasurementCycle() {
    const session = robot.session;
    if (!session?.active || cycleBusy) return;
    cycleBusy = true;
    const measurementId = `${session.id}-M${session.measurements.length + 1}`;
    try {
        robot.phase = 'measuring';
        await sendCommand('stop');
        addEvent('stop', `Остановка для замера на ${robot.telemetry?.railKm || '—'}`);
        const resultPromise = waitForMeasurement();
        await sendCommand('measure', { measurementId });
        const result = await resultPromise;
        const telemetry = robot.telemetry;
        const measurement = {
            id: measurementId, measuredAt: new Date().toISOString(), hardness: Number(result.hardness),
            status: result.status === 'defect' ? 'defect' : 'ok', marked: false,
            lat: telemetry.lat, lon: telemetry.lon, rawLat: telemetry.rawLat, rawLon: telemetry.rawLon,
            railKm: telemetry.railKm, railDistanceMeters: telemetry.railDistanceMeters,
            distanceMeters: telemetry.distanceMeters, wheelTicks: telemetry.wheelTicks,
        };
        session.measurements.push(measurement);
        session.lastMeasurementDistance = telemetry.distanceMeters;
        addEvent('measurement', `${measurement.railKm}: ${measurement.hardness}, ${measurement.status}`);
        if (measurement.status === 'defect') {
            robot.phase = 'marking';
            await sendCommand('mark', { measurementId }, 4000);
            measurement.marked = true;
            addEvent('mark', `${measurement.railKm}: отправлена команда маркировки`);
        }
        persistSessions();
        if (session.active && !session.paused) {
            await sendCommand('move', { direction: 'forward' });
            robot.phase = 'running';
        } else if (session.active) {
            robot.phase = 'paused';
        }
    } catch (error) {
        robot.phase = 'error';
        addEvent('error', error.message);
        try { await sendCommand('stop'); } catch (_) {}
    } finally {
        cycleBusy = false;
    }
}

async function startRun() {
    if (!robot.connection.connected) throw new Error('ESP не подключена');
    if (!robot.telemetry?.onRail) throw new Error('GPS находится дальше допустимого расстояния от выбранного пути');
    if (robot.session?.active) throw new Error('Рейс уже запущен');
    const now = new Date();
    robot.session = {
        id: now.toISOString().replace(/[:.]/g, '-'), startedAt: now.toISOString(), finishedAt: null,
        active: true, paused: false, status: 'running', startRailKm: robot.telemetry.railKm,
        currentRailKm: robot.telemetry.railKm, distanceMeters: 0, lastMeasurementDistance: 0,
        path: [], measurements: [], events: [],
    };
    robot.latestMeasurement = null;
    await sendCommand('reset');
    await sendCommand('move', { direction: 'forward' });
    robot.phase = 'running';
    addEvent('start', `Рейс запущен на ${robot.telemetry.railKm}`);
    persistSessions();
}

async function pauseRun() {
    if (!robot.session?.active) throw new Error('Активного рейса нет');
    robot.session.paused = true;
    await sendCommand('stop');
    robot.phase = 'paused';
    addEvent('pause', 'Рейс поставлен на паузу');
    persistSessions();
}

async function resumeRun() {
    if (!robot.session?.active || !robot.session.paused) throw new Error('Рейс не находится на паузе');
    robot.session.paused = false;
    await sendCommand('move', { direction: 'forward' });
    robot.phase = 'running';
    addEvent('resume', 'Рейс продолжен');
}

async function stopRun() {
    await sendCommand('stop');
    if (robot.session?.active) {
        robot.session.active = false;
        robot.session.paused = false;
        robot.session.status = 'completed';
        robot.session.finishedAt = new Date().toISOString();
        addEvent('finish', 'Рейс завершён оператором');
        storedSessions.push(robot.session);
        persistSessions();
    }
    manualDirection = null;
    robot.phase = 'idle';
}

function calculateMetrics(session) {
    if (!session) return null;
    const values = session.measurements.map(item => item.hardness).filter(Number.isFinite);
    return {
        distanceMeters: Math.round(session.distanceMeters * 10) / 10,
        measurements: session.measurements.length,
        defects: session.measurements.filter(item => item.status === 'defect').length,
        marked: session.measurements.filter(item => item.marked).length,
        averageHardness: values.length ? Math.round(values.reduce((a, b) => a + b, 0) / values.length) : null,
        minHardness: values.length ? Math.min(...values) : null,
        maxHardness: values.length ? Math.max(...values) : null,
        durationSeconds: Math.round((Date.parse(session.finishedAt || new Date()) - Date.parse(session.startedAt)) / 1000),
    };
}

function publicState() {
    return { ...robot, metrics: calculateMetrics(robot.session), savedSessions: storedSessions.length };
}

setInterval(() => {
    const direction = robot.phase === 'running' ? 'forward' : manualDirection;
    if (direction && robot.connection.connected) {
        try { sendRaw({ type: 'command', command: 'heartbeat', direction }); } catch (_) {}
    }
}, 300);

function readJsonBody(req) {
    return new Promise((resolve, reject) => {
        let body = '';
        req.on('data', chunk => { body += chunk; if (body.length > 1024 * 1024) reject(new Error('request too large')); });
        req.on('end', () => { try { resolve(body ? JSON.parse(body) : {}); } catch (error) { reject(error); } });
        req.on('error', reject);
    });
}

function sendJson(res, status, value) {
    res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
    res.end(JSON.stringify(value));
}

async function handleApi(req, res, pathname) {
    if (req.method === 'GET' && pathname === '/api/state') { sendJson(res, 200, publicState()); return true; }
    if (req.method === 'GET' && pathname === '/api/sessions') { sendJson(res, 200, storedSessions); return true; }
    const actions = {
        '/api/run/start': startRun, '/api/run/pause': pauseRun,
        '/api/run/resume': resumeRun, '/api/run/stop': stopRun,
    };
    if (req.method === 'POST' && actions[pathname]) {
        try { await actions[pathname](); sendJson(res, 200, { ok: true, state: publicState() }); }
        catch (error) { sendJson(res, 409, { ok: false, error: error.message }); }
        return true;
    }
    if (req.method === 'POST' && pathname === '/api/manual') {
        try {
            const body = await readJsonBody(req);
            if (robot.session?.active && !robot.session.paused) throw new Error('Сначала поставьте автоматический рейс на паузу');
            if (!['forward', 'back', 'stop'].includes(body.command)) throw new Error('Неизвестная команда');
            manualDirection = body.command === 'stop' ? null : body.command;
            await sendCommand(body.command === 'stop' ? 'stop' : 'move',
                body.command === 'stop' ? {} : { direction: body.command });
            robot.phase = body.command === 'stop' ? (robot.session?.active ? 'paused' : 'idle') : 'manual';
            sendJson(res, 200, { ok: true });
        } catch (error) { sendJson(res, 409, { ok: false, error: error.message }); }
        return true;
    }
    if ((req.method === 'GET' || req.method === 'POST') && ['/forward', '/back', '/stop'].includes(pathname)) {
        const command = pathname.slice(1);
        manualDirection = command === 'stop' ? null : command;
        try {
            await sendCommand(command === 'stop' ? 'stop' : 'move', command === 'stop' ? {} : { direction: command });
            sendJson(res, 200, { ok: true });
        } catch (error) { sendJson(res, 503, { ok: false, error: error.message }); }
        return true;
    }
    if (req.method === 'GET' && pathname === '/track') {
        const session = robot.session;
        sendJson(res, 200, {
            path: session?.path || [], measurements: session?.measurements || [],
            currentPosition: robot.telemetry ? { lat: robot.telemetry.lat, lon: robot.telemetry.lon } : null,
        });
        return true;
    }
    return false;
}

function serveStaticFile(res, root, relativePath) {
    let decoded;
    try { decoded = decodeURIComponent(relativePath); } catch (_) { res.writeHead(400).end(); return; }
    const filePath = path.resolve(root, decoded);
    const relative = path.relative(root, filePath);
    if (relative.startsWith('..') || path.isAbsolute(relative)) { res.writeHead(403).end(); return; }
    const types = {
        '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8',
        '.js': 'application/javascript; charset=utf-8', '.geojson': 'application/geo+json; charset=utf-8',
        '.png': 'image/png',
    };
    fs.readFile(filePath, (error, data) => {
        if (error) { res.writeHead(error.code === 'ENOENT' ? 404 : 500).end(); return; }
        res.writeHead(200, { 'Content-Type': types[path.extname(filePath)] || 'application/octet-stream' });
        res.end(data);
    });
}

const server = http.createServer(async (req, res) => {
    const pathname = new URL(req.url, 'http://localhost').pathname;
    try { if (await handleApi(req, res, pathname)) return; }
    catch (error) { sendJson(res, 500, { error: error.message }); return; }
    if (req.method !== 'GET') { res.writeHead(404).end(); return; }
    if (pathname === '/' || pathname === '/index.html') serveStaticFile(res, ROOT, 'index.html');
    else if (pathname.startsWith('/vendor/leaflet/')) {
        serveStaticFile(res, path.join(ROOT, 'vendor', 'leaflet'), pathname.slice('/vendor/leaflet/'.length));
    } else if (pathname.startsWith('/data/')) {
        serveStaticFile(res, path.join(ROOT, 'data'), pathname.slice('/data/'.length));
    } else res.writeHead(404).end();
});

server.listen(HTTP_PORT, '0.0.0.0', () => {
    console.log(`Панель: http://localhost:${HTTP_PORT}`);
    console.log(INTERNAL_EMULATOR ? 'Режим: встроенный эмулятор ESP' : 'Режим: ESP по Serial');
    connectEsp();
});

process.on('SIGINT', async () => {
    try { if (robot.connection.connected) await sendCommand('stop', {}, 500); } catch (_) {}
    if (transport?.close) transport.close();
    server.close(() => process.exit(0));
});
