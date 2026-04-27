importScripts("./solver_integer.js");

const modulePromise = createTchiselIntegerModule({
  locateFile(path) {
    if (path.endsWith(".wasm")) return "./solver_integer.wasm";
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
      solver: "integer",
      error: String(err && err.message ? err.message : err),
    });
  }
};
