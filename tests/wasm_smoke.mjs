// Runs the same workloads and options through the WebAssembly build and the
// native binary and requires identical JSON output.
// Usage: node tests/wasm_smoke.mjs build-wasm/site/sched.js build/sched
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const [modulePath, nativeBin] = process.argv.slice(2);
const { default: createSched } = await import(pathToFileURL(path.resolve(modulePath)).href);
let out = [];
const engine = await createSched({ print: (s) => out.push(s), printErr: (s) => out.push("ERR " + s) });

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), "..");
const cases = [
  ["examples/mixed.csv", ["--algo=all,opt-np", "--gap"]],
  ["examples/basic.csv", ["--algo=all", "--cores=2", "--cs-cost=1", "--quantum=3"]],
  ["examples/mixed.csv", ["--algo=sjf,srtf,hrrn", "--predict=ewma", "--alpha=0.3", "--tau0=4"]],
  ["examples/mixed.csv", ["--algo=prio-p,mlfq,lottery", "--aging=2", "--mlfq-boost=10", "--seed=9"]],
];
let failures = 0;
for (const [file, args] of cases) {
  const csv = fs.readFileSync(path.join(root, file), "utf8");
  engine.FS.writeFile("/w.csv", csv);
  out = [];
  const code = engine.callMain(["--input=/w.csv", "--format=json", ...args]);
  const wasm = out.join("\n") + "\n";
  const native = execFileSync(nativeBin, [`--input=${path.join(root, file)}`, "--format=json", ...args],
                              { encoding: "utf8" });
  const ok = code === 0 && wasm === native;
  console.log(`${ok ? "ok  " : "FAIL"} ${file} ${args.join(" ")}`);
  if (!ok) failures++;
}
// an invalid workload reports an error and a non-zero exit code
engine.FS.writeFile("/bad.csv", "pid,arrival,burst\n1,0,0\n");
out = [];
const code = engine.callMain(["--input=/bad.csv", "--format=json"]);
const errorOk = code === 1 && out.some((l) => l.includes("burst of P1 must be > 0"));
console.log(`${errorOk ? "ok  " : "FAIL"} invalid input is rejected`);
if (!errorOk) failures++;
process.exit(failures ? 1 : 0);
