// solver_multiradical_worker.js
// Defers loading until solve args arrive, so main() auto-runs with the right arguments.
// No need for callMain or INVOKE_RUN=0.

var stdout = '';
var stderr = '';

onmessage = function(e) {
    var digit = e.data.digit;
    var target = e.data.target;
    var maxDigits = e.data.maxDigits || 9;

    console.log('[multiradical] solving: digit=' + digit + ' target=' + target + ' max=' + maxDigits);

    var done = false;

    function sendResult() {
        if (done) return;
        done = true;
        console.log('[multiradical] done. stdout=' + stdout.length + ' bytes');
        postMessage({ type: 'result', result: parseOutput(stdout, stderr, target) });
    }

    // Configure Module BEFORE loading the Emscripten JS
    var Module = {
        arguments: [String(digit), String(target), String(maxDigits)],
        print: function(text) { stdout += text + '\n'; },
        printErr: function(text) {
            stderr += text + '\n';
            var m = text.match(/S\[(\d+)\]\s*=\s*(\d+)/);
            if (m) postMessage({ type: 'progress', n: parseInt(m[1]), size: parseInt(m[2]) });
        },
        quit: function(status) {
            // Called when main() returns — send result
            console.log('[multiradical] quit with status ' + status);
            sendResult();
        },
        onExit: function(status) {
            console.log('[multiradical] onExit with status ' + status);
            sendResult();
        }
    };

    // Make it global so Emscripten picks it up
    self.Module = Module;

    try {
        importScripts('solver_multiradical.js');
    } catch(err) {
        console.log('[multiradical] importScripts error:', err);
        postMessage({ type: 'result', result: { found: false, error: String(err) } });
        return;
    }

    // If main() ran synchronously, stdout has content already
    if (stdout.length > 0) {
        sendResult();
    }
    // Otherwise, quit/onExit callback will fire when async WASM finishes
};

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