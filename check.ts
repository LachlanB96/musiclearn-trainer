// check.ts: the Trainer's check (project rule 5). Nothing lands on main until it passes.
//
//   bun check.ts                         every part
//   bun check.ts --only ts,build,smoke   some of them
//
// Run it from the main checkout or a worktree: it checks the folder it's in. Don't wrap it in warden-run: it queues
// the build and the smoke test with the warden itself. One check runs at a time (they share the build and data
// folders), so a second one refuses while %LOCALAPPDATA%\MusicLearnTrainer\check.lock names a live process.
//
//   ts     TypeScript 7 over check.ts and test/, then test/*.test.ts, through the megaproject's check.cts
//   build  build.cmd: the VST3 and the Standalone. The main checkout builds in %LOCALAPPDATA%\MusicLearnTrainer\build
//          (what install.cmd installs), a worktree in ...\check-build. A build folder whose CMake cache was made
//          from another source folder is emptied first, since CMake refuses to use it.
//   smoke  test/smoke.ts on that build: the Standalone, opened without focus on the right-most screen with a fresh
//          data folder, asked through WebView2's debugging port whether its page works as the plugin's

import { bun, command } from "megaproject";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "fs";
import path from "path";

export const PARTS = ["ts", "build", "smoke"] as const;
export type Part = (typeof PARTS)[number];

/** The parts --only names, in the order they run; every part without it */
export function parseOnly(argv: string[]): Part[] {
  const i = argv.indexOf("--only");
  if (i < 0) return [...PARTS];
  const asked = (argv[i + 1] ?? "").split(",").map(s => s.trim()).filter(Boolean);
  const unknown = asked.filter(a => !(PARTS as readonly string[]).includes(a));
  if (!asked.length || unknown.length) throw new Error(`--only takes some of ${PARTS.join(",")}${unknown.length ? `, not ${unknown.join(",")}` : ""}`);
  return PARTS.filter(p => asked.includes(p));
}

/** Two Windows folder paths name the same folder, as CMake compares them: slashes and case aside */
export function sameFolder(a: string, b: string): boolean {
  const norm = (p: string) => p.replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase();
  return norm(a) === norm(b);
}

/** The source folder a CMake cache was made from (CMAKE_HOME_DIRECTORY), or null when it doesn't say */
export function cacheSource(cache: string): string | null {
  const m = /^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$/m.exec(cache);
  return m ? m[1].trim() : null;
}

/** Where a checkout builds: the main checkout where install.cmd takes it from, a worktree in a folder of its own */
export function buildDir(localAppData: string, mainCheckout: boolean): string {
  return path.join(localAppData, "MusicLearnTrainer", mainCheckout ? "build" : "check-build");
}

function isMainCheckout(root: string): boolean {
  const out = Bun.spawnSync(["git", "-C", root, "rev-parse", "--path-format=absolute", "--git-dir", "--git-common-dir"], { stdout: "pipe", stderr: "pipe" });
  if (out.exitCode !== 0) throw new Error(`git rev-parse failed in ${root}: ${out.stderr.toString().trim()}`);
  const [dir, common] = out.stdout.toString().trim().split(/\r?\n/);
  return sameFolder(dir, common);
}

function alive(pid: number): boolean {
  if (!Number.isInteger(pid) || pid <= 0) return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
}

/** Takes the check's lock, or throws naming the live check that holds it; returns its release */
function lock(file: string): () => void {
  mkdirSync(path.dirname(file), { recursive: true });
  try {
    writeFileSync(file, String(process.pid), { flag: "wx" });
  } catch (e) {
    if ((e as NodeJS.ErrnoException).code !== "EEXIST") throw e;
    const holder = Number(readFileSync(file, "utf8").trim());
    if (alive(holder)) throw new Error(`another Trainer check (pid ${holder}) is running: ${file}`);
    writeFileSync(file, String(process.pid)); // its check ended without letting go
  }
  return () => rmSync(file, { force: true });
}

/** Empties a build folder whose CMake cache was made from another source folder */
function readyBuildDir(dir: string, root: string): void {
  const cacheFile = path.join(dir, "CMakeCache.txt");
  if (!existsSync(cacheFile)) return;
  const source = cacheSource(readFileSync(cacheFile, "utf8"));
  if (source === null) throw new Error(`${cacheFile} doesn't say which source folder it was made from (CMAKE_HOME_DIRECTORY)`);
  if (sameFolder(source, root)) return;
  say(`build: ${dir} was made from ${source}; emptying it for ${root}`);
  rmSync(dir, { recursive: true, force: true });
}

const say = (text: string) => console.log(`[check] ${text}`);

function run(argv: string[], env: Record<string, string> = {}): number {
  return Bun.spawnSync(argv, { stdio: ["inherit", "inherit", "inherit"], env: { ...process.env, ...env } }).exitCode;
}

export function main(argv: string[]): number {
  const parts = parseOnly(argv);
  const root = import.meta.dir;
  const local = process.env.LOCALAPPDATA;
  if (!local) throw new Error("LOCALAPPDATA isn't set");
  const dir = buildDir(local, isMainCheckout(root));
  const release = lock(path.join(local, "MusicLearnTrainer", "check.lock"));
  const results: [Part, string][] = [];
  try {
    for (const part of parts) {
      if (part === "smoke" && results.some(([p, r]) => p === "build" && r !== "passed")) {
        results.push([part, "skipped: the build failed"]);
        continue;
      }
      say(`${part}: starting`);
      let code: number;
      if (part === "ts") {
        code = run([bun(), Bun.resolveSync("megaproject/check.cts", root)]);
      } else if (part === "build") {
        readyBuildDir(dir, root);
        const warden = [...command("warden-run"), "--name", "trainer-build", "--cpus", "8", "--mem", "8", "--max", "30m", "--"];
        code = run([...warden, "cmd.exe", "/d", "/c", path.win32.join(root, "build.cmd")], { TRAINER_BUILD_DIR: dir });
      } else {
        const warden = [...command("warden-run"), "--name", "trainer-smoke", "--cpus", "2", "--mem", "4", "--max", "5m", "--"];
        code = run([...warden, bun(), path.join(root, "test", "smoke.ts"), dir]);
      }
      results.push([part, code === 0 ? "passed" : `FAILED (exit ${code})`]);
    }
  } finally {
    release();
  }
  for (const [part, result] of results) say(`${part}: ${result}`);
  return results.every(([, r]) => r === "passed") ? 0 : 1;
}

if (import.meta.main) {
  try {
    process.exit(main(process.argv.slice(2)));
  } catch (e) {
    console.error(`[check] FAILED: ${(e as Error).message}`);
    process.exit(1);
  }
}
