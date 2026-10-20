// Runs the module's _start with the given arguments under a WASI that answers for a page: the
// arguments, no environment, a monotonic and a wall clock, standard output and error captured, and
// no files. Any other WASI call reports ENOSYS.

const SUCCESS = 0;
const EBADF = 8;
const ENOSYS = 52;
const CHARACTER_DEVICE = 2;

class Exit extends Error {
  constructor(code) {
    super(`exit ${code}`);
    this.code = code;
  }
}

self.onmessage = async ({ data }) => {
  const argv = ["sleeve-bench", ...data.args].map((a) => new TextEncoder().encode(`${a}\0`));
  const out = { 1: [], 2: [] };
  const pending = { 2: "" };
  let memory = null;
  const view = () => new DataView(memory.buffer);
  const bytes = () => new Uint8Array(memory.buffer);
  const decoder = new TextDecoder();

  const known = {
    args_sizes_get(count, size) {
      view().setUint32(count, argv.length, true);
      view().setUint32(size, argv.reduce((n, a) => n + a.length, 0), true);
      return SUCCESS;
    },
    args_get(pointers, buffer) {
      let at = buffer;
      argv.forEach((a, i) => {
        view().setUint32(pointers + 4 * i, at, true);
        bytes().set(a, at);
        at += a.length;
      });
      return SUCCESS;
    },
    environ_sizes_get(count, size) {
      view().setUint32(count, 0, true);
      view().setUint32(size, 0, true);
      return SUCCESS;
    },
    environ_get: () => SUCCESS,
    clock_time_get(id, _precision, at) {
      const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
      view().setBigUint64(at, ns, true);
      return SUCCESS;
    },
    fd_write(fd, iovs, count, written) {
      const v = view();
      let total = 0;
      for (let i = 0; i < count; i++) {
        const base = v.getUint32(iovs + i * 8, true);
        const length = v.getUint32(iovs + i * 8 + 4, true);
        const text = decoder.decode(bytes().slice(base, base + length));
        if (fd === 1) out[1].push(text);
        if (fd === 2) {
          const lines = (pending[2] + text).split("\n");
          pending[2] = lines.pop();
          for (const line of lines) {
            out[2].push(line);
            self.postMessage({ kind: "line", text: line });
          }
        }
        total += length;
      }
      v.setUint32(written, total, true);
      return fd === 1 || fd === 2 ? SUCCESS : EBADF;
    },
    fd_fdstat_get(fd, at) {
      if (fd > 2) return EBADF;
      const v = view();
      v.setUint8(at, CHARACTER_DEVICE);
      v.setUint16(at + 2, 0, true);
      v.setBigUint64(at + 8, 0n, true);
      v.setBigUint64(at + 16, 0n, true);
      return SUCCESS;
    },
    fd_fdstat_set_flags: () => SUCCESS,
    fd_close: () => SUCCESS,
    fd_prestat_get: () => EBADF,
    proc_exit(code) {
      throw new Exit(code);
    },
  };

  const module = await WebAssembly.compileStreaming(fetch("sleeve-bench.wasm"));
  const wasi = {};
  for (const { module: from, name } of WebAssembly.Module.imports(module)) {
    if (from === "wasi_snapshot_preview1") wasi[name] = known[name] ?? (() => ENOSYS);
  }
  const instance = await WebAssembly.instantiate(module, { wasi_snapshot_preview1: wasi });
  memory = instance.exports.memory;
  let exit = 0;
  let error = null;
  try {
    instance.exports._start();
  } catch (e) {
    if (e instanceof Exit) exit = e.code;
    else {
      exit = -1;
      error = String(e);
    }
  }
  if (pending[2]) out[2].push(pending[2]);
  self.postMessage({ kind: "done", exit, error, stdout: out[1].join(""), stderr: out[2].join("\n") });
};
