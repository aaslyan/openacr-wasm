// ui_render.js -- Terminal renderer for ui.* schema
// Reads ui ssim data files, loads samp_mdb WASM, renders to terminal
// Usage: node ui_render.js

const fs = require('fs');
const path = require('path');
const readline = require('readline');

const DATA_DIR = path.join(__dirname, '..', 'data', 'ui');
const WASM_DIR = path.join(__dirname, 'build');

// ANSI color codes
const COLORS = {
    default: '', black: '\x1b[30m', red: '\x1b[31m', green: '\x1b[32m',
    yellow: '\x1b[33m', blue: '\x1b[34m', magenta: '\x1b[35m',
    cyan: '\x1b[36m', white: '\x1b[37m',
};
const BG_COLORS = {
    default: '', black: '\x1b[40m', red: '\x1b[41m', green: '\x1b[42m',
    yellow: '\x1b[43m', blue: '\x1b[44m', magenta: '\x1b[45m',
    cyan: '\x1b[46m', white: '\x1b[47m',
};
const RESET = '\x1b[0m';
const BOLD = '\x1b[1m';
const UNDERLINE = '\x1b[4m';

// Border characters (single-line box drawing)
const BORDER = {
    none: { tl: ' ', tr: ' ', bl: ' ', br: ' ', h: ' ', v: ' ' },
    single: { tl: '\u250c', tr: '\u2510', bl: '\u2514', br: '\u2518', h: '\u2500', v: '\u2502' },
    double: { tl: '\u2554', tr: '\u2557', bl: '\u255a', br: '\u255d', h: '\u2550', v: '\u2551' },
    rounded: { tl: '\u256d', tr: '\u256e', bl: '\u2570', br: '\u256f', h: '\u2500', v: '\u2502' },
    ascii: { tl: '+', tr: '+', bl: '+', br: '+', h: '-', v: '|' },
};

// Tree drawing characters
const TREE = { branch: '\u251c', last: '\u2514', pipe: '\u2502', dash: '\u2500', space: ' ' };

// Parse ssim line into object
function parseSsim(line) {
    line = line.trim();
    if (!line || line.startsWith('#')) return null;
    const obj = {};
    // Match key:value or key:"quoted value" pairs
    const re = /(\w+):(?:"([^"]*)"|(\S*))/g;
    let m;
    while ((m = re.exec(line)) !== null) {
        obj[m[1]] = m[2] !== undefined ? m[2] : m[3];
    }
    return obj;
}

// Load ssim file into array of objects
function loadSsim(filename) {
    const filepath = path.join(DATA_DIR, filename);
    if (!fs.existsSync(filepath)) return [];
    return fs.readFileSync(filepath, 'utf8')
        .split('\n')
        .map(parseSsim)
        .filter(Boolean);
}

// Screen buffer
class Screen {
    constructor(rows, cols) {
        this.rows = rows;
        this.cols = cols;
        this.buf = [];
        this.styles = [];
        this.clear();
    }
    clear() {
        this.buf = Array.from({ length: this.rows }, () => Array(this.cols).fill(' '));
        this.styles = Array.from({ length: this.rows }, () => Array(this.cols).fill(''));
    }
    put(row, col, ch, style) {
        if (row >= 0 && row < this.rows && col >= 0 && col < this.cols) {
            this.buf[row][col] = ch;
            if (style) this.styles[row][col] = style;
        }
    }
    putStr(row, col, str, style, maxW) {
        const w = maxW || str.length;
        for (let i = 0; i < w && col + i < this.cols; i++) {
            this.put(row, col + i, i < str.length ? str[i] : ' ', style || '');
        }
    }
    render() {
        let out = '\x1b[H\x1b[2J'; // clear screen, home cursor
        for (let r = 0; r < this.rows; r++) {
            let line = '';
            let prevStyle = '';
            for (let c = 0; c < this.cols; c++) {
                const s = this.styles[r][c];
                if (s !== prevStyle) {
                    line += RESET + s;
                    prevStyle = s;
                }
                line += this.buf[r][c];
            }
            line += RESET;
            out += line + '\n';
        }
        process.stdout.write(out);
    }
}

// Build style string from ui.Style record
function buildStyle(styleRec) {
    if (!styleRec) return '';
    let s = '';
    if (styleRec.fg && COLORS[styleRec.fg]) s += COLORS[styleRec.fg];
    if (styleRec.bg && BG_COLORS[styleRec.bg]) s += BG_COLORS[styleRec.bg];
    if (styleRec.bold === 'Y') s += BOLD;
    if (styleRec.underline === 'Y') s += UNDERLINE;
    return s;
}

// Main
async function main() {
    // Load UI data
    const windows = loadSsim('window.ssim');
    const styles = loadSsim('style.ssim');
    const widgets = loadSsim('widget.ssim');
    const bindings = loadSsim('binding.ssim');
    const columns = loadSsim('column.ssim');
    const tableCfgs = loadSsim('table_cfg.ssim');
    const treeCfgs = loadSsim('tree_cfg.ssim');
    const keyMaps = loadSsim('key_map.ssim');

    // Index by key
    const styleMap = {};
    styles.forEach(s => styleMap[s.style] = s);
    const bindingMap = {};
    bindings.forEach(b => bindingMap[b.p_widget] = b);
    const columnMap = {};
    columns.forEach(c => {
        if (!columnMap[c.p_widget]) columnMap[c.p_widget] = [];
        columnMap[c.p_widget].push(c);
    });
    const tableCfgMap = {};
    tableCfgs.forEach(t => tableCfgMap[t.p_widget] = t);
    const treeCfgMap = {};
    treeCfgs.forEach(t => treeCfgMap[t.p_widget] = t);

    // Load WASM module
    const SampMdb = require(path.join(WASM_DIR, 'samp_mdb.js'));
    const M = await SampMdb();
    M.init();

    // Seed some data
    M.insertSsim('samp_mdb.User  user:alice  email:alice@example.com  role:developer');
    M.insertSsim('samp_mdb.User  user:bob  email:bob@example.com  role:manager');
    M.insertSsim('samp_mdb.User  user:carol  email:carol@example.com  role:designer');
    M.insertSsim('samp_mdb.Project  project:webapp  description:"Web Application"  status:active');
    M.insertSsim('samp_mdb.Project  project:api  description:"REST API"  status:active');
    M.insertSsim('samp_mdb.Project  project:mobile  description:"Mobile App"  status:planning');
    M.insertSsim('samp_mdb.Task  task:task1  title:"Build login"  priority:1  status:open  p_project:webapp  p_user:alice');
    M.insertSsim('samp_mdb.Task  task:task2  title:"Write docs"  priority:2  status:open  p_project:api  p_user:bob');
    M.insertSsim('samp_mdb.Task  task:task3  title:"Design UI"  priority:1  status:open  p_project:webapp  p_user:carol');
    M.insertSsim('samp_mdb.Task  task:task4  title:"API auth"  priority:3  status:open  p_project:api  p_user:alice');
    M.insertSsim('samp_mdb.Task  task:task5  title:"Prototype"  priority:1  status:open  p_project:mobile  p_user:bob');

    // Data accessors by ctype
    const dataAccessors = {
        'samp_mdb.User': { list: () => M.userList(), fields: ['user', 'email', 'role'] },
        'samp_mdb.Project': { list: () => M.projectList(), fields: ['project', 'description', 'status'] },
        'samp_mdb.Task': { list: () => M.taskList(), fields: ['task', 'title', 'priority', 'status', 'p_project', 'p_user'] },
        'samp_mdb.Quote': { list: () => M.quoteList(), fields: ['symbol', 'price', 'ts'] },
    };

    // Window
    const win = windows[0];
    const screen = new Screen(parseInt(win.rows), parseInt(win.cols));

    // Focus state
    const focusable = widgets.filter(w => w.selection === 'single');
    let focusIdx = 0;
    const scrollState = {};
    widgets.forEach(w => scrollState[w.widget] = { offset: 0, selected: 0 });

    // Expanded tree nodes
    const expanded = { webapp: true, api: true, mobile: false };

    function getStyle(widget) {
        if (widget.p_style && styleMap[widget.p_style]) {
            return buildStyle(styleMap[widget.p_style]);
        }
        return '';
    }

    function drawBorder(widget, style) {
        const r = parseInt(widget.row), c = parseInt(widget.col);
        const w = parseInt(widget.w), h = parseInt(widget.h);
        const borderType = styleMap[widget.p_style]?.border || 'none';
        const b = BORDER[borderType] || BORDER.none;
        if (borderType === 'none') return;

        screen.put(r, c, b.tl, style);
        screen.put(r, c + w - 1, b.tr, style);
        screen.put(r + h - 1, c, b.bl, style);
        screen.put(r + h - 1, c + w - 1, b.br, style);
        for (let i = 1; i < w - 1; i++) {
            screen.put(r, c + i, b.h, style);
            screen.put(r + h - 1, c + i, b.h, style);
        }
        for (let i = 1; i < h - 1; i++) {
            screen.put(r + i, c, b.v, style);
            screen.put(r + i, c + w - 1, b.v, style);
        }
        // Title
        if (widget.title) {
            const t = ' ' + widget.title + ' ';
            screen.putStr(r, c + 2, t, style + BOLD);
        }
    }

    function drawLabel(widget) {
        const style = getStyle(widget);
        screen.putStr(parseInt(widget.row), parseInt(widget.col), widget.text || '', style, parseInt(widget.w));
    }

    function drawStatusBar(widget) {
        const style = getStyle(widget);
        const row = parseInt(widget.row);
        const focused = focusable[focusIdx]?.widget || '';
        const text = widget.text + '  [focus: ' + focused + ']';
        screen.putStr(row, parseInt(widget.col), text, style, parseInt(widget.w));
    }

    function drawTable(widget) {
        const style = getStyle(widget);
        const isFocused = focusable[focusIdx]?.widget === widget.widget;
        const r = parseInt(widget.row), c = parseInt(widget.col);
        const w = parseInt(widget.w), h = parseInt(widget.h);
        const cfg = tableCfgMap[widget.widget] || {};
        const cols = columnMap[widget.widget] || [];
        const binding = bindingMap[widget.widget];

        drawBorder(widget, style);

        let dataRows = [];
        if (binding && dataAccessors[binding.source_ctype]) {
            dataRows = dataAccessors[binding.source_ctype].list();
        }

        const contentR = r + 1;
        const contentH = h - 2;
        let row = contentR;

        // Header
        if (cfg.header === 'Y' && cols.length > 0) {
            let colOff = c + 1;
            cols.forEach(col => {
                screen.putStr(row, colOff, col.title || col.field, style + BOLD + UNDERLINE, parseInt(col.w));
                colOff += parseInt(col.w) + 1;
            });
            row++;
        }

        // Data rows
        const ss = scrollState[widget.widget];
        const maxRows = contentH - (cfg.header === 'Y' ? 1 : 0);
        for (let i = ss.offset; i < dataRows.length && row < r + h - 1; i++) {
            const rec = dataRows[i];
            const isSelected = isFocused && i === ss.selected;
            const rowStyle = isSelected ? buildStyle(styleMap.selected) : style;
            let colOff = c + 1;
            cols.forEach(col => {
                const val = String(rec[col.field] || '');
                screen.putStr(row, colOff, val, rowStyle, parseInt(col.w));
                colOff += parseInt(col.w) + 1;
            });
            row++;
        }
    }

    function drawTree(widget) {
        const style = getStyle(widget);
        const isFocused = focusable[focusIdx]?.widget === widget.widget;
        const r = parseInt(widget.row), c = parseInt(widget.col);
        const w = parseInt(widget.w), h = parseInt(widget.h);
        const cfg = treeCfgMap[widget.widget] || {};
        const binding = bindingMap[widget.widget];
        const indent = parseInt(cfg.indent) || 2;

        drawBorder(widget, style);

        let projects = [];
        if (binding && dataAccessors[binding.source_ctype]) {
            projects = dataAccessors[binding.source_ctype].list();
        }
        const tasks = dataAccessors['samp_mdb.Task']?.list() || [];

        const ss = scrollState[widget.widget];
        let row = r + 1;
        let flatIdx = 0;

        for (let pi = 0; pi < projects.length && row < r + h - 1; pi++) {
            const proj = projects[pi];
            const isExp = expanded[proj.project];
            const marker = isExp ? TREE.branch : TREE.last;
            const isSelected = isFocused && flatIdx === ss.selected;
            const rowStyle = isSelected ? buildStyle(styleMap.selected) : style;
            const prefix = (isExp ? '\u25bc ' : '\u25b6 ');
            screen.putStr(row, c + 1, prefix + proj.project + ' - ' + proj.description, rowStyle, w - 2);
            row++;
            flatIdx++;

            if (isExp) {
                const projTasks = tasks.filter(t => t.p_project === proj.project);
                for (let ti = 0; ti < projTasks.length && row < r + h - 1; ti++) {
                    const t = projTasks[ti];
                    const isTaskSelected = isFocused && flatIdx === ss.selected;
                    const taskStyle = isTaskSelected ? buildStyle(styleMap.selected) : style;
                    const treeChar = ti === projTasks.length - 1 ? TREE.last : TREE.branch;
                    const line = '  ' + treeChar + TREE.dash + ' ' + t.task + ': ' + t.title;
                    screen.putStr(row, c + 1, line, taskStyle, w - 2);
                    row++;
                    flatIdx++;
                }
            }
        }
    }

    function draw() {
        screen.clear();
        widgets.forEach(widget => {
            if (widget.visible !== 'Y') return;
            switch (widget.type) {
                case 'label': drawLabel(widget); break;
                case 'statusbar': drawStatusBar(widget); break;
                case 'table': drawTable(widget); break;
                case 'tree': drawTree(widget); break;
            }
        });
        screen.render();
    }

    // Keyboard input
    readline.emitKeypressEvents(process.stdin);
    if (process.stdin.isTTY) process.stdin.setRawMode(true);

    process.stdin.on('keypress', (str, key) => {
        if (!key) return;
        const focused = focusable[focusIdx]?.widget;
        const ss = focused ? scrollState[focused] : null;
        const binding = focused ? bindingMap[focused] : null;
        let dataLen = 0;
        if (binding && dataAccessors[binding.source_ctype]) {
            dataLen = dataAccessors[binding.source_ctype].list().length;
        }
        // For tree, count flat items
        if (focused && widgets.find(w => w.widget === focused)?.type === 'tree') {
            const projects = dataAccessors['samp_mdb.Project']?.list() || [];
            const tasks = dataAccessors['samp_mdb.Task']?.list() || [];
            dataLen = 0;
            projects.forEach(p => {
                dataLen++;
                if (expanded[p.project]) {
                    dataLen += tasks.filter(t => t.p_project === p.project).length;
                }
            });
        }

        // Match key against keymaps
        const keyName = key.name === 'return' ? 'Enter' : key.name === 'tab' ? 'Tab' : str;
        const matching = keyMaps.filter(km =>
            km.key === keyName && (km.p_widget === '' || km.p_widget === focused)
        );

        matching.forEach(km => {
            switch (km.action) {
                case 'quit':
                    process.stdout.write('\x1b[2J\x1b[H');
                    process.exit(0);
                    break;
                case 'focus_next':
                    focusIdx = (focusIdx + 1) % focusable.length;
                    break;
                case 'navigate_next':
                    if (ss && ss.selected < dataLen - 1) ss.selected++;
                    break;
                case 'navigate_prev':
                    if (ss && ss.selected > 0) ss.selected--;
                    break;
                case 'toggle_expand':
                    if (focused) {
                        // Find which project is selected
                        const projects = dataAccessors['samp_mdb.Project']?.list() || [];
                        const tasks = dataAccessors['samp_mdb.Task']?.list() || [];
                        let idx = 0;
                        for (const p of projects) {
                            if (idx === ss.selected) {
                                expanded[p.project] = !expanded[p.project];
                                break;
                            }
                            idx++;
                            if (expanded[p.project]) {
                                idx += tasks.filter(t => t.p_project === p.project).length;
                            }
                        }
                    }
                    break;
            }
        });

        // Also handle ctrl-c
        if (key.ctrl && key.name === 'c') {
            process.stdout.write('\x1b[2J\x1b[H');
            process.exit(0);
        }

        draw();
    });

    // Initial draw
    draw();
}

main().catch(e => { console.error(e); process.exit(1); });
