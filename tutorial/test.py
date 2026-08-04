#!/usr/bin/env python3
"""blockmalloc tutorial test — run case binaries, measure time and exit code."""

from __future__ import annotations
import argparse, csv, os, subprocess, sys, time
from pathlib import Path

RED, GREEN, YELLOW, NC = "\033[0;31m", "\033[0;32m", "\033[1;33m", "\033[0m"
ROOT = Path(__file__).resolve().parent.parent
FAIL_CSV = (ROOT / "tutorial" / "test_failures.csv").resolve()
TIMEOUT = 30  # seconds per case


def discover(build_dir: Path) -> list[Path]:
    """返回 build/tutorial/ 下所有可执行 case，按名称排序。"""
    cases = sorted(build_dir.rglob("[0-9][0-9]_*"))
    return [c for c in cases if c.is_file() and os.access(c, os.X_OK)]


def build() -> bool:
    out = ROOT / "build"
    out.mkdir(exist_ok=True)
    r = subprocess.run(
        ["cmake", "-DCMAKE_BUILD_TYPE=Release", ".."],
        capture_output=True, text=True, timeout=60, cwd=str(out))
    if r.returncode != 0:
        print(f"{RED}❌ cmake failed:{NC}\n{r.stderr}")
        return False
    r = subprocess.run(
        ["make", "-j$(nproc)"],
        capture_output=True, text=True, timeout=120, cwd=str(out))
    if r.returncode != 0:
        print(f"{RED}❌ make failed:{NC}\n{r.stderr}")
        return False
    return True


def main():
    ap = argparse.ArgumentParser(description="blockmalloc tutorial test")
    ap.add_argument("--filter", default="", help="filter by case name")
    ap.add_argument("--no-build", action="store_true", help="skip cmake + make")
    ap.add_argument("--errorexit", action="store_true", help="exit on first error")
    ap.add_argument("--repeat", type=int, default=1, help="repeat each case N times")
    args = ap.parse_args()

    build_dir = ROOT / "build" / "tutorial"

    if not args.no_build:
        if not build():
            sys.exit(1)
        print(f"{GREEN}✅ build ok{NC}")

    cases = [c for c in discover(build_dir) if args.filter in c.name]
    if not cases:
        print(f"{YELLOW}no case binaries found in {build_dir}{NC}")
        sys.exit(0)

    passed = failed = 0
    failures: list[dict] = []

    for case in cases:
        rel = case.name
        for run_i in range(args.repeat):
            label = f"{rel} [{run_i+1}/{args.repeat}]" if args.repeat > 1 else rel
            try:
                t0 = time.monotonic()
                r = subprocess.run([str(case)], capture_output=True, text=True,
                                   timeout=TIMEOUT)
                elapsed = time.monotonic() - t0

                if r.returncode != 0:
                    print(f"{RED}❌ {label}: exit={r.returncode} ⏱ {elapsed:.3f}s{NC}")
                    print(f"   stdout: {r.stdout.strip()[:300]}")
                    failed += 1
                    failures.append({"case": rel, "run": run_i + 1,
                                     "exit": r.returncode,
                                     "time": f"{elapsed:.3f}",
                                     "stdout": r.stdout.strip()[:500]})
                    if args.errorexit:
                        _write_csv(failures)
                        sys.exit(1)
                else:
                    print(f"{GREEN}✅ {label}: exit=0 ⏱ {elapsed:.3f}s{NC}")
                    if r.stdout.strip():
                        print(f"   {r.stdout.strip()[:200]}")
                    passed += 1

            except subprocess.TimeoutExpired:
                print(f"{RED}❌ {label}: timeout >{TIMEOUT}s{NC}")
                failed += 1
                failures.append({"case": rel, "run": run_i + 1,
                                 "exit": "timeout", "time": f">{TIMEOUT}",
                                 "stdout": ""})
                if args.errorexit:
                    _write_csv(failures)
                    sys.exit(1)

    _write_csv(failures)
    total = passed + failed
    print(f"{YELLOW}══ {GREEN}PASS:{passed}{YELLOW}  {RED}FAIL:{failed}{YELLOW}  "
          f"TOTAL:{total} ══{NC}")
    print(f"report: {FAIL_CSV}")
    sys.exit(0 if failed == 0 else 1)


def _write_csv(failures: list[dict]) -> None:
    FAIL_CSV.write_text("case,run,exit,time,stdout\n", encoding="utf-8")
    if not failures:
        return
    with open(FAIL_CSV, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=["case", "run", "exit", "time", "stdout"])
        w.writeheader()
        for row in failures:
            w.writerow(row)


if __name__ == "__main__":
    main()
