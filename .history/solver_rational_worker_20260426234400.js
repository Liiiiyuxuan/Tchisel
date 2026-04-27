// solver_rational_worker.js
// Web Worker for the rational Tchisla solver via WASM
// Handles both Emscripten patterns: global Module and MODULARIZE factory

let stdout = '';
let stderr = '';
let wasmReady = false;
let pendingJob = null;
let solverModule = null;

function setupPrint() {
    return {
        noInitialRun: true,
        locateFile: function(path) {
            // Ensure .wasm file is found in the same directory
            return path;
        },
        print: function(text) { stdout += text + '\n'; },
        printErr: function(text) {
            stderr += text + '\n';
            var m = text.match(/S\[(\d+)\]\s*=\s*(\d+)/);
            if (m) postMessage({ type: 'progress', n: parseInt(m[1]), size: parseInt(m[2]) });
        },
        onRuntimeInitialized: function() {
            wasmReady = true;
            solverModule = this;
            if (pendingJob) {
                runSolve(pendingJob);
                pendingJob = null;
            }
        }
    };
}

// Try non-modularized first (global Module)
var Module = setupPrint();

try {
    importScripts('solver_rational.js');
} catch(e) {
    postMessage({ type: 'result', result: { found: false, error: 'Failed to load solver_rational.js: ' + e.message } });
}

// If Emscripten used MODULARIZE, Module might now be a factory function
// (or createModule might exist)
var factory = (typeof createModule === 'function') ? createModule
            : (typeof Module === 'function') ? Module
            : null;

if (factory && !wasmReady) {
    factory(setupPrint()).then(function(mod) {
        solverModule = mod;
        wasmReady = true;
        if (pendingJob) {
            runSolve(pendingJob);
            pendingJob = null;
        }
    });
}

onmessage = function(e) {
    if (wasmReady) {
        runSolve(e.data);
    } else {
        pendingJob = e.data;
    }
};

function runSolve(data) {
    var digit = data.digit;
    var target = data.target;
    var maxDigits = data.maxDigits || 9;

    stdout = '';
    stderr = '';

    try {
        var mod = solverModule || Module;
        mod.callMain([String(digit), String(target), String(maxDigits)]);
    } catch (err) {
        postMessage({
            type: 'result',
            result: { found: false, error: String(err), message: 'WASM execution error: ' + String(err) }
        });
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
    var formulaMatch = out.match(new RegExp('\\s*' + target + '\\s*=\\s*(.+)'));
    var formula = formulaMatch ? formulaMatch[1].trim() : '';

    return { found: true, digits: digits, formula: formula, stats: parseStats(err) };
}

function parseStats(err) {
    var stats = [];
    var re = /S\[(\d+)\]\s*=\s*(\d+)/g;
    var m;
    while ((m = re.exec(err)) !== null) {
        stats.push([parseInt(m[1]), parseInt(m[2])]);
    }
    return stats;
}// solver_rational_worker.js
// Web Worker for the rational Tchisla solver via WASM
// Handles both Emscripten patterns: global Module and MODULARIZE factory

let stdout = '';
let stderr = '';
let wasmReady = false;
let pendingJob = null;
let solverModule = null;

function setupPrint() {
    return {
        noInitialRun: true,
        locateFile: function(path) {
            // Ensure .wasm file is found in the same directory
            return path;
        },
        print: function(text) { stdout += text + '\n'; },
        printErr: function(text) {
            stderr += text + '\n';
            var m = text.match(/S\[(\d+)\]\s*=\s*(\d+)/);
            if (m) postMessage({ type: 'progress', n: parseInt(m[1]), size: parseInt(m[2]) });
        },
        onRuntimeInitialized: function() {
            wasmReady = true;
            solverModule = this;
            if (pendingJob) {
                runSolve(pendingJob);
                pendingJob = null;
            }
        }
    };
}

// Try non-modularized first (global Module)
var Module = setupPrint();

try {
    importScripts('solver_rational.js');
} catch(e) {
    postMessage({ type: 'result', result: { found: false, error: 'Failed to load solver_rational.js: ' + e.message } });
}

// If Emscripten used MODULARIZE, Module might now be a factory function
// (or createModule might exist)
var factory = (typeof createModule === 'function') ? createModule
            : (typeof Module === 'function') ? Module
            : null;

if (factory && !wasmReady) {
    factory(setupPrint()).then(function(mod) {
        solverModule = mod;
        wasmReady = true;
        if (pendingJob) {
            runSolve(pendingJob);
            pendingJob = null;
        }
    });
}

onmessage = function(e) {
    if (wasmReady) {
        runSolve(e.data);
    } else {
        pendingJob = e.data;
    }
};

function runSolve(data) {
    var digit = data.digit;
    var target = data.target;
    var maxDigits = data.maxDigits || 9;

    stdout = '';
    stderr = '';

    try {
        var mod = solverModule || Module;
        mod.callMain([String(digit), String(target), String(maxDigits)]);
    } catch (err) {
        postMessage({
            type: 'result',
            result: { found: false, error: String(err), message: 'WASM execution error: ' + String(err) }
        });
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
    var formulaMatch = out.match(new RegExp('\\s*' + target + '\\s*=\\s*(.+)'));
    var formula = formulaMatch ? formulaMatch[1].trim() : '';

    return { found: true, digits: digits, formula: formula, stats: parseStats(err) };
}

function parseStats(err) {
    var stats = [];
    var re = /S\[(\d+)\]\s*=\s*(\d+)/g;
    var m;
    while ((m = re.exec(err)) !== null) {
        stats.push([parseInt(m[1]), parseInt(m[2])]);
    }
    return stats;
}