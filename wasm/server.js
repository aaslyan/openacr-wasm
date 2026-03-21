// server.js -- samp_mdb sync server
// Same WASM module runs here (server) and in the browser (client)
const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');

const PORT = 9090;
const BUILD_DIR = path.join(__dirname, 'build');
const QUOTE_INTERVAL_MS = 1000;    // generate a quote every second
const QUOTE_WINDOW_MS = 10 * 60 * 1000; // keep last 10 minutes

// Load the same WASM module that the browser uses
const SampMdb = require('./build/samp_mdb.js');

const MIME = {
    '.html': 'text/html',
    '.js':   'application/javascript',
    '.wasm': 'application/wasm',
    '.css':  'text/css',
};

// HTTP server for static files
const server = http.createServer((req, res) => {
    let url = req.url === '/' ? '/index.html' : req.url;
    let file = path.join(BUILD_DIR, url);
    let ext = path.extname(file);
    if (!fs.existsSync(file)) {
        res.writeHead(404);
        res.end('Not found');
        return;
    }
    res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
});

// WebSocket server
const wss = new WebSocketServer({ server });

// ============= Quote feed =============
// Simulated price series for 3 symbols
const SYMBOLS = ['AAPL', 'GOOG', 'TSLA'];
const quoteState = {};
SYMBOLS.forEach(sym => {
    quoteState[sym] = {
        price: sym === 'AAPL' ? 185 : sym === 'GOOG' ? 175 : 250,
        base: sym === 'AAPL' ? 185 : sym === 'GOOG' ? 175 : 250,
        vol: sym === 'AAPL' ? 1.2 : sym === 'GOOG' ? 1.5 : 3.0,
    };
});

// Ring buffer of quotes, pruned to QUOTE_WINDOW_MS
let quotes = [];

function generateQuote() {
    const now = Date.now();
    const sym = SYMBOLS[Math.floor(Math.random() * SYMBOLS.length)];
    const st = quoteState[sym];
    // Random walk with mean reversion toward base price
    const meanRevert = (st.base - st.price) * 0.02;
    const noise = (Math.random() - 0.5) * 2 * st.vol;
    const jump = Math.random() < 0.05 ? (Math.random() - 0.5) * st.vol * 5 : 0;
    st.price = Math.max(st.base * 0.7, Math.min(st.base * 1.3, st.price + meanRevert + noise + jump));
    const q = { ts: now, symbol: sym, price: Math.round(st.price * 100) / 100 };
    quotes.push(q);
    // Prune older than 10 minutes
    const cutoff = now - QUOTE_WINDOW_MS;
    while (quotes.length > 0 && quotes[0].ts < cutoff) {
        quotes.shift();
    }
    return q;
}

function broadcastAll(msg) {
    const data = JSON.stringify(msg);
    wss.clients.forEach(client => {
        if (client.readyState === 1) {
            client.send(data);
        }
    });
}

SampMdb().then(M => {
    M.init();
    console.log('samp_mdb WASM initialized on server');

    // Seed some initial data
    const seed = [
        'samp_mdb.User  user:alice  email:alice@example.com  role:developer',
        'samp_mdb.User  user:bob  email:bob@example.com  role:manager',
        'samp_mdb.Project  project:webapp  description:"Web Application"  status:active',
        'samp_mdb.Project  project:api  description:"REST API"  status:active',
        'samp_mdb.Task  task:task1  title:"Build login page"  priority:1  status:open  p_project:webapp  p_user:alice',
        'samp_mdb.Task  task:task2  title:"Write API docs"  priority:2  status:open  p_project:api  p_user:bob',
    ];
    seed.forEach(s => M.insertSsim(s));
    console.log('Seeded', M.userCount(), 'users');

    // Start quote generator
    setInterval(() => {
        const q = generateQuote();
        broadcastAll({ type: 'quote', quote: q });
    }, QUOTE_INTERVAL_MS);
    console.log('Quote feed started (' + SYMBOLS.join(', ') + ') every ' + QUOTE_INTERVAL_MS + 'ms');

    // Get all records as ssim strings
    function allRecordsAsSsim() {
        let lines = [];
        let users = M.userList();
        for (let i = 0; i < users.length; i++) {
            lines.push(M.toSsim('user', users[i].user));
        }
        let projects = M.projectList();
        for (let i = 0; i < projects.length; i++) {
            lines.push(M.toSsim('project', projects[i].project));
        }
        let tasks = M.taskList();
        for (let i = 0; i < tasks.length; i++) {
            lines.push(M.toSsim('task', tasks[i].task));
        }
        return lines;
    }

    // Broadcast to all connected clients except sender
    function broadcast(msg, sender) {
        wss.clients.forEach(client => {
            if (client !== sender && client.readyState === 1) {
                client.send(msg);
            }
        });
    }

    wss.on('connection', (ws) => {
        console.log('Client connected');

        // Send full state on connect (hydration)
        let records = allRecordsAsSsim();
        ws.send(JSON.stringify({ type: 'sync', records: records }));
        console.log('Sent', records.length, 'records to client');

        // Send quote history (last 10 min)
        ws.send(JSON.stringify({ type: 'quote_history', quotes: quotes }));
        console.log('Sent', quotes.length, 'quote history to client');

        ws.on('message', (data) => {
            let msg = JSON.parse(data.toString());
            if (msg.type === 'insert') {
                let ok = M.insertSsim(msg.ssim);
                console.log('Insert from client:', ok ? 'OK' : 'FAIL', msg.ssim);
                if (ok) {
                    broadcast(JSON.stringify({ type: 'insert', ssim: msg.ssim }), ws);
                }
                ws.send(JSON.stringify({ type: 'ack', ok: ok, ssim: msg.ssim }));
            }
        });

        ws.on('close', () => {
            console.log('Client disconnected');
        });
    });

    server.listen(PORT, () => {
        console.log(`Server running at http://localhost:${PORT}`);
    });
});
