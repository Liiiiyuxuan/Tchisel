// solver_rational_worker.js

var stdout = '';
var stderr = '';
var wasmReady = false;
var pendingJob = null;
var solverModule = null;

function log(msg) {
    console.log('[rational worker] ' + msg);
    postMessage({ type: 'log', msg: '[rational] ' + msg });
}

var moduleConfig = {
    noInitialRun: true,
    locateFile: function(path) {
        log('locateFile: ' + path);
        return path;
    },
    print: function(text) { stdout += text + '\n'; },
    printErr: function(text) {
        stderr += text + '\n';
        var m = text.match(/S\[(\d+)\]\s*=\s*(\d+)/);
        if (m) postMessage({ type: 'progress', n: parseInt(m[1]), size: parseInt(m[2]) });
    },
    onRuntimeInitialized: function() {
        log('WASM runtime initialized');
        wasmReady = true;
        solverModule = Module;
        if (pendingJob) {
            runSolve(pendingJob);
            pendingJob = null;
        }
    },
    onAbort: function(what) {
        log('ABORT: ' + what);
    }
};

var Module = moduleConfig;

log('loading solver_rational.js...');

try {
    importScripts('solver_rational.js');
    log('importScripts done. Module type=' + typeof Module + ' callMain type=' + typeof (Module && Module.callMain));
} catch(e) {
    log('importScripts FAILED: ' + e.message);
    postMessage({ type: 'result', result: { found: false, error: 'Failed to load: ' + e.message } });
}

// Check if Emscripten used MODULARIZE (Module became a factory function)
if (typeof Module === 'function') {
    log('Module is a factory function (MODULARIZE). Calling it...');
    Module(moduleConfig).then(function(mod) {
        log('Factory resolved. callMain type=' + typeof mod.callMain);
        solverModule = mod;
        wasmReady = true;
        if (pendingJob) {
            runSolve(pendingJob);
            pendingJob = null;
        }
    }).catch(function(err) {
        log('Factory FAILED: ' + err);
        postMessage({ type: 'result', result: { found: false, error: 'WASM init failed: ' + err } });
    });
} else if (typeof createModule === 'function') {
    log('createModule exists (MODULARIZE). Calling it...');
    createModule(moduleConfig).then(function(mod) {
        log('createModule resolved. callMain type=' + typeof mod.callMain);
        solverModule = mod;
        wasmReady = true;
        if (pendingJob) {
            runSolve(pendingJob);
            pendingJob = null;
        }
    }).catch(function(err) {
        log('createModule FAILED: ' + err);
        postMessage({ type: 'result', result: { found: false, error: 'WASM init failed: ' + err } });
    });
} else {
    log('Module is an object (non-modularized). wasmReady=' + wasmReady);
    if (!wasmReady) {
        log('Waiting for onRuntimeInitialized...');
    }
}

onmessage = function(e) {
    log('received message: ' + JSON.stringify(e.data));
    if (wasmReady) {
        runSolve(e.data);
    } else {
        log('WASM not ready yet, queuing job');
        pendingJob = e.data;
    }
};

function runSolve(data) {
    var digit = data.digit;
    var target = data.target;
    var maxDigits = data.maxDigits || 9;

    log('running solve: digit=' + digit + ' target=' + target + ' max=' + maxDigits);

    stdout = '';
    stderr = '';

    var mod = solverModule || Module;

    if (!mod || typeof mod.callMain !== 'function') {
        var err = 'callMain not available. Module type=' + typeof mod + ', keys=' + (mod ? Object.keys(mod).slice(0, 10).join(',') : 'null');
        log('ERROR: ' + err);
        postMessage({ type: 'result', result: { found: false, error: err } });
        return;
    }

    try {
        mod.callMain([String(digit), String(target), String(maxDigits)]);
        log('callMain finished. stdout length=' + stdout.length + ' stderr length=' + stderr.length);
        log('stdout: ' + stdout.slice(0, 200));
    } catch (err) {
        log('callMain THREW: ' + err);
        postMessage({ type: 'result', result: { found: false, error: String(err), message: 'WASM execution error' } });
        return;
    }

    postMessage({ type: 'result', result: parseOutput(stdout, stderr, target) });
}

function parseOutput(out, err, target) {
    var solvedMatch = out.match(/SOLVED in (\d+) digit/);
    if (!solvedMatch) {
        return { found: false, message: 'No solution found', stats: parseStats(err) };
    }
    var digits = parseInt(solvedMatch[1]);
    var re = new RegExp('\\s*' + target + '\\s*=\\s*(.+)');
    var formulaMatch = out.match(re);
    var formula = formulaMatch ? formulaMatch[1].trim() : '';
    return { found: true, digits: digits, formula: formula, stats: parseStats(err) };
}

function parseStats(err) {
    var stats = [];
    var re = /S\[(\d+)\]\s*=\s*(\d+)/g;
    var m;
    while ((m = re.exec(err)) !== null) stats.push([parseInt(m[1]), parseInt(m[2])]);
    return stats;
}