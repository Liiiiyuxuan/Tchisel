// ╔═══════════════════════════════════════════════════════════╗
// ║  tchisel_wasm_controller.js                              ║
// ║  Manages integer, rational, and multiradical WASM solvers║
// ║  running in parallel Web Workers.                        ║
// ╚═══════════════════════════════════════════════════════════╝

class TchiselController {
    constructor() {
        this.workers = {};
        this.jobId = 0;
        this.currentJob = null;
    }

    /**
     * Solve a Tchisla puzzle.
     * @param {number} digit - The digit (1-9)
     * @param {number} target - Target number
     * @param {number} maxDigits - Maximum digits to search
     * @param {object} callbacks:
     *   onFirstResult(result) — called when the fastest solver finishes
     *   onImprovement(result) — called when a better solution is found
     *   onStage(stage) — called when a solver starts/finishes
     *   onDone(result) — called when all solvers finish
     * @returns {number} jobId for cancellation
     */
    solve(digit, target, maxDigits, callbacks = {}) {
        this.cancel(); // cancel any previous job

        const jobId = ++this.jobId;
        const solvers = [
            { name: 'integer',      worker: 'solver_integer_worker.js' },
            { name: 'rational',     worker: 'solver_rational_worker.js' },
            { name: 'multiradical', worker: 'solver_multiradical_worker.js' },
        ];

        let best = null;
        let finishedCount = 0;
        const totalCount = solvers.length;
        const results = [];
        const workers = [];

        const isBetter = (a, b) => {
            if (!a || !a.found) return false;
            if (!b || !b.found) return true;
            if (a.digits < b.digits) return true;
            if (a.digits === b.digits && (a.formula || '').length < (b.formula || '').length) return true;
            return false;
        };

        const onWorkerResult = (solverName, result) => {
            if (jobId !== this.jobId) return; // stale job

            result.source = solverName;
            results.push(result);
            finishedCount++;

            console.log(`[tchisel-wasm] ${solverName}: ${result.found ? result.digits + ' digits' : 'not found'}`);

            if (isBetter(result, best)) {
                const isFirst = !best;
                best = result;

                if (isFirst && callbacks.onFirstResult) {
                    callbacks.onFirstResult(result);
                } else if (!isFirst && callbacks.onImprovement) {
                    callbacks.onImprovement(result);
                }
            }

            if (callbacks.onStage) {
                const running = solvers
                    .filter((_, i) => !results.some(r => r.source === solvers[i].name))
                    .map(s => s.name);
                callbacks.onStage(running.length > 0 ? running[0] : '');
            }

            if (finishedCount >= totalCount) {
                this.currentJob = null;
                if (callbacks.onDone) {
                    callbacks.onDone(best, results);
                }
            }
        };

        for (const solver of solvers) {
            try {
                const w = new Worker(solver.worker);

                w.onmessage = (e) => {
                    const data = e.data;
                    if (data.type === 'result') {
                        onWorkerResult(solver.name, data.result);
                        w.terminate();
                    } else if (data.type === 'progress') {
                        // Optional: solver can post set-size updates
                        console.log(`[tchisel-wasm] ${solver.name} S[${data.n}] = ${data.size}`);
                    }
                };

                w.onerror = (err) => {
                    console.error(`[tchisel-wasm] ${solver.name} error:`, err.message);
                    onWorkerResult(solver.name, {
                        found: false,
                        source: solver.name,
                        error: err.message,
                        message: `Worker error: ${err.message}`,
                    });
                    w.terminate();
                };

                w.postMessage({ digit, target, maxDigits });
                workers.push(w);

            } catch (err) {
                console.warn(`[tchisel-wasm] failed to start ${solver.name}:`, err.message);
                onWorkerResult(solver.name, {
                    found: false,
                    source: solver.name,
                    error: err.message,
                    message: `Failed to start worker: ${err.message}`,
                });
            }
        }

        this.currentJob = { jobId, workers };
        return jobId;
    }

    /** Cancel the current solve */
    cancel() {
        if (this.currentJob) {
            for (const w of this.currentJob.workers) {
                try { w.terminate(); } catch (_) {}
            }
            this.currentJob = null;
        }
        this.jobId++;
    }

    /** Check if a solve is running */
    get isRunning() {
        return this.currentJob !== null;
    }
}

// Global singleton
window.tchiselController = new TchiselController();