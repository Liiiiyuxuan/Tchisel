// Use this from your main frontend JS instead of fetch("/solve").
// Example:
//   const result = await solveWithTchiselWasm({ digit: 7, target: 2018, maxDigits: 8 });
//   console.log(result);

const TCHISEL_SOLVER_WORKERS = [
  { id: "integer", url: "./solver_integer_worker.js" },
  { id: "rational", url: "./solver_rational_worker.js" },
  { id: "multiradical", url: "./solver_multiradical_worker.js" },
];

let tchiselNextRequestId = 1;

function runTchiselWorker(spec, payload, timeoutMs) {
  return new Promise((resolve) => {
    const worker = new Worker(spec.url);
    const requestId = tchiselNextRequestId++;
    const timer = setTimeout(() => {
      worker.terminate();
      resolve({ ok: false, found: false, solver: spec.id, error: "timeout" });
    }, timeoutMs);

    worker.onmessage = (event) => {
      if (event.data && event.data.requestId !== requestId) return;
      clearTimeout(timer);
      worker.terminate();
      resolve(event.data);
    };

    worker.onerror = (event) => {
      clearTimeout(timer);
      worker.terminate();
      resolve({ ok: false, found: false, solver: spec.id, error: event.message || "worker error" });
    };

    worker.postMessage({ requestId, payload });
  });
}

function chooseBestTchiselResult(results) {
  const found = results.filter((r) => r && r.ok && r.found);
  if (found.length === 0) {
    return results.find((r) => r && r.ok) || results[0] || { ok: false, found: false, error: "no solver result" };
  }

  found.sort((a, b) => {
    const da = Number.isFinite(a.digits) ? a.digits : 999;
    const db = Number.isFinite(b.digits) ? b.digits : 999;
    if (da !== db) return da - db;
    return String(a.expression || "").length - String(b.expression || "").length;
  });
  return found[0];
}

async function solveWithTchiselWasm(payload, options = {}) {
  const timeoutMs = options.timeoutMs ?? 60000;
  const results = await Promise.all(
    TCHISEL_SOLVER_WORKERS.map((spec) => runTchiselWorker(spec, payload, timeoutMs))
  );
  return chooseBestTchiselResult(results);
}
