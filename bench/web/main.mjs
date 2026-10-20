// Runs sleeve-bench.wasm in a worker with the arguments in the page's query (?args=bench+--seeds+1000
// &label=NAME) and posts what it printed to /report?label=NAME.

const query = new URLSearchParams(location.search);
const args = (query.get("args") ?? "bench").split(" ").filter((a) => a.length > 0);
const label = query.get("label") ?? "browser";
const log = document.getElementById("log");

const worker = new Worker("worker.mjs", { type: "module" });
worker.onmessage = async ({ data }) => {
  if (data.kind === "line") {
    log.textContent += `\n${data.text}`;
    return;
  }
  let report = null;
  try {
    report = JSON.parse(data.stdout);
  } catch {
    report = null;
  }
  const body = JSON.stringify({
    browser: navigator.userAgent,
    cross_origin_isolated: self.crossOriginIsolated,
    hardware_concurrency: navigator.hardwareConcurrency,
    args,
    exit: data.exit,
    error: data.error ?? null,
    stderr: data.stderr,
    stdout: report === null ? data.stdout : null,
    report,
  }, null, 2);
  await fetch(`/report?label=${encodeURIComponent(label)}`, { method: "POST", body });
  log.textContent += `\nreported (exit ${data.exit})`;
};
worker.postMessage({ args });
