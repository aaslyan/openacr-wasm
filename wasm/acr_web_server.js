// acr_web_server.js -- Serves acr_web WASM browser + ssim data files
// Usage: node acr_web_server.js [port]

const http = require('http');
const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const PORT = parseInt(process.argv[2] || '9091');
const BUILD_DIR = path.join(__dirname, 'build');
const DATA_DIR = path.join(__dirname, '..', 'data');

const MIME = {
    '.html': 'text/html',
    '.js':   'application/javascript',
    '.wasm': 'application/wasm',
    '.css':  'text/css',
    '.ssim': 'text/plain',
};

// Collect all ssim files from data/ directory
function getAllSsimFiles() {
    const files = [];
    const dirs = fs.readdirSync(DATA_DIR);
    for (const d of dirs) {
        const subdir = path.join(DATA_DIR, d);
        if (!fs.statSync(subdir).isDirectory()) continue;
        const entries = fs.readdirSync(subdir);
        for (const f of entries) {
            if (f.endsWith('.ssim')) {
                files.push(path.join(d, f));
            }
        }
    }
    return files;
}

// Load all ssim data into a single string (for bulk loading into WASM)
function loadAllSsimData() {
    const files = getAllSsimFiles();
    let data = '';
    for (const f of files) {
        const content = fs.readFileSync(path.join(DATA_DIR, f), 'utf8');
        data += content;
    }
    return data;
}

// API: run amc_vis and return ASCII output
function runAmcVis(ctype) {
    try {
        const binDir = path.join(__dirname, '..', 'bin');
        const result = execSync(`${binDir}/amc_vis ${ctype} -xref 2>&1`, {
            timeout: 5000,
            encoding: 'utf8',
            cwd: path.join(__dirname, '..'),
        });
        return result;
    } catch (e) {
        return e.stdout || 'amc_vis failed';
    }
}

// API: run acr -t and return tree output
function runAcrTree(pattern) {
    try {
        const binDir = path.join(__dirname, '..', 'bin');
        const result = execSync(`${binDir}/acr -t ${pattern} 2>&1`, {
            timeout: 5000,
            encoding: 'utf8',
            cwd: path.join(__dirname, '..'),
        });
        return result;
    } catch (e) {
        return e.stdout || 'acr failed';
    }
}

const server = http.createServer((req, res) => {
    const url = new URL(req.url, `http://localhost:${PORT}`);

    // API endpoints
    if (url.pathname === '/api/ssim-data') {
        res.writeHead(200, { 'Content-Type': 'text/plain', 'Access-Control-Allow-Origin': '*' });
        res.end(loadAllSsimData());
        return;
    }

    if (url.pathname === '/api/amc-vis') {
        const ctype = url.searchParams.get('ctype') || '';
        res.writeHead(200, { 'Content-Type': 'text/plain', 'Access-Control-Allow-Origin': '*' });
        res.end(runAmcVis(ctype));
        return;
    }

    if (url.pathname === '/api/acr-tree') {
        const pattern = url.searchParams.get('pattern') || '';
        res.writeHead(200, { 'Content-Type': 'text/plain', 'Access-Control-Allow-Origin': '*' });
        res.end(runAcrTree(pattern));
        return;
    }

    // Static files
    let filePath = url.pathname === '/' ? '/acr_web.html' : url.pathname;
    let file = path.join(BUILD_DIR, filePath);
    let ext = path.extname(file);
    if (!fs.existsSync(file)) {
        res.writeHead(404);
        res.end('Not found');
        return;
    }
    res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
});

server.listen(PORT, () => {
    console.log(`acr_web server at http://localhost:${PORT}`);
    console.log(`  ssim data: ${getAllSsimFiles().length} files from data/`);
});
