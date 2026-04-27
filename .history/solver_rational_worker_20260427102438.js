// solver_rational_worker.js
// Uses Emscripten MODULARIZE factory: createTchiselRationalModule

importScripts('solver_rational.js');

onmessage = async function(e) {
    var digit = e.data.digit;
    var target = e.data.target;
    var maxDigits = e.data.maxDigits || 9;
    var stdout = '';
    var stderr = '';

    try {
        await createTchiselRationalModule({
            arguments: [String(digit), String(target), String(maxDigits)],
            print: function(text) { stdout += text + '\n'; },
            printErr: function(text) {
                stderr += text + '\n';
                var m = text.match(/S\[(\d+)\]\s*=\s*(\d+)/);
                if (m) postMessage({ type: 'progress', n: parseInt(m[1]), size: parseInt(m[2]) });
            }
        });
    } catch (err) {
        postMessage({ type: 'result', result: { found: false, error: String(err) } });
        return;
    }

    postMessage({ type: 'result', result: parseOutput(stdout, stderr, target) });
};

function parseOutput(out, err, target) {
    var solvedMatch = out.match(/SOLVED in (\d+) digit/);
    if (!solvedMatch) return { found: false, message: 'No solution found', stats: parseStats(err) };
    var digits = parseInt(solvedMatch[1]);
    var formulaMatch = out.match(new RegExp('\\s*' + target + '\\s*=\\s*(.+)'));
    var formula = formulaMatch ? formulaMatch[1].trim() : '';
    return { found: true, digits: digits, formula: formula, stats: parseStats(err) };
}

function parseStats(err) {
    var stats = [], re = /S\[(\d+)\]\s*=\s*(\d+)/g, m;
    while ((m = re.exec(err)) !== null) stats.push([parseInt(m[1]), parseInt(m[2])]);
    return stats;
}