importScripts("./solver_multiradical.js");

const modulePromise = createTchiselMultiradicalModule({
  locateFile(path) {
    if (path.endsWith(".wasm")) return "./solver_multiradical.wasm";
    return path;
  },
});

self.onmessage = async (event) => {
  const requestId = event.data && event.data.requestId;
  const payload = event.data && event.data.payload ? event.data.payload : event.data;

  try {
    const module = await modulePromise;
    const outputJson = module.solveJson(JSON.stringify(payload));
    const result = JSON.parse(outputJson);
    self.postMessage({ requestId, ...result });
  } catch (err) {
    self.postMessage({
      requestId,
      ok: false,
      found: false,
      solver: "multiradical",
      error: String(err && err.message ? err.message : err),
    });
  }
};
