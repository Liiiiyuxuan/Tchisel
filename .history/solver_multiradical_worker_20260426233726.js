// solver_multiradical_worker.js
// Web Worker that runs the multiradical/symbolic Tchisla solver via WASM

let stdout = '';
let stderr = '';

var Module = {
    print: (text) => { stdout += text + '\n'; },
    printErr: (text) => {
        stderr += text + '\n';
        const m = text.match(/S\[(\d+)\]\s*=\s*(\d+)/);
        if (m) postMessage({ type: 'progress', n: parseInt(m[1]), size: parseInt(m[2]) });
    },
    noInitialRun: true,
    onRuntimeInitialized: () => {}
};

importScripts('solver_multiradical.js');

onmessage = function(e) {
    const { digit, target, maxDigits } = e.data;
    stdout = '';
    stderr = '';

    try {
        Module.callMain([String(digit), String(target), String(maxDigits || 9)]);
    } catch (err) {
        postMessage({ type: 'result', result: { found: false, error: err.message } });
        return;
    }

    postMessage({ type: 'result', result: parseOutput(stdout, stderr, digit, target) });
};

function parseOutput(out, err, digit, target) {
    const solvedMatch = out.match(/SOLVED in (\d+) digit/);
    if (!solvedMatch) return { found: false, message: 'No solution found', stats: parseStats(err) };
    const digits = parseInt(solvedMatch[1]);
    const formulaMatch = out.match(new RegExp(`\\s*${target}\\s*=\\s*(.+)`));
    const formula = formulaMatch ? formulaMatch[1].trim() : '';
    return { found: true, digits, formula, stats: parseStats(err) };
}

function parseStats(err) {
    const stats = [];
    const re = /S\[(\d+)\]\s*=\s*(\d+)/g;
    let m;
    while ((m = re.exec(err)) !== null) stats.push([parseInt(m[1]), parseInt(m[2])]);
    return stats;
}