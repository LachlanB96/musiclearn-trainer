import { describe, expect, test } from "bun:test";
import path from "path";
import { buildDir, cacheSource, parseOnly, sameFolder } from "../check.ts";

describe("parseOnly", () => {
  test("every part, in order, without --only", () => {
    expect(parseOnly([])).toEqual(["ts", "build", "smoke"]);
  });

  test("the parts named, in the order they run", () => {
    expect(parseOnly(["--only", "smoke,ts"])).toEqual(["ts", "smoke"]);
  });

  test("refuses a part it doesn't have, and an empty list", () => {
    expect(() => parseOnly(["--only", "ts,unity"])).toThrow("not unity");
    expect(() => parseOnly(["--only"])).toThrow("--only takes some of ts,build,smoke");
  });
});

describe("sameFolder", () => {
  test("slashes, case and a trailing slash aside", () => {
    expect(sameFolder("C:/Users/LachlanB/AI/Musica/code/Trainer", "c:\\users\\lachlanb\\ai\\musica\\code\\trainer\\")).toBe(true);
  });

  test("a junction's other name is another folder, as CMake sees it", () => {
    expect(sameFolder("C:/Users/LachlanB/AI/MusicLearn/code/Trainer", "C:/Users/LachlanB/AI/Musica/code/Trainer")).toBe(false);
  });
});

describe("cacheSource", () => {
  test("reads CMAKE_HOME_DIRECTORY", () => {
    const cache = "# This is the CMakeCache file.\nCMAKE_BUILD_TYPE:STRING=Release\nCMAKE_HOME_DIRECTORY:INTERNAL=C:/Users/LachlanB/AI/Musica/code/Trainer\r\nWebView2_root_dir:PATH=x\n";
    expect(cacheSource(cache)).toBe("C:/Users/LachlanB/AI/Musica/code/Trainer");
  });

  test("null when the cache doesn't say", () => {
    expect(cacheSource("CMAKE_BUILD_TYPE:STRING=Release\n")).toBeNull();
  });
});

describe("buildDir", () => {
  test("the main checkout builds where install.cmd looks, a worktree beside it", () => {
    expect(buildDir("C:/L", true)).toBe(path.join("C:/L", "MusicLearnTrainer", "build"));
    expect(buildDir("C:/L", false)).toBe(path.join("C:/L", "MusicLearnTrainer", "check-build"));
  });
});
