# CSA Kernel Refcount Checker

`KernelRefcountChecker.cpp` is the kernel-refcount CSA baseline used in the
paper *Efficient Verification of Reference Counting Consistency via DeDyck-CFL
Reachability*. It checks reference-count balance with symbolic execution.
This Linux kernel reference-count checker is implemented following the approach
described in the **PyRefcon** paper,
[*Detecting Memory Errors in Python Native Code by Tracking Object Lifecycle
with Reference Count*](https://doi.org/10.1109/ASE56229.2023.00198) (ASE 2023),
adapting its reference-count tracking approach from Python native code to Linux
kernel reference-counting APIs.

Use **LLVM/Clang 15.0.7**, the version used in the paper. You need Git, CMake,
Ninja, and a C/C++ compiler (for example, `clang-15` and `clang++-15`). This checker
is compiled **in-tree into Clang**, not loaded as a standalone plugin.
The paper's kernel subjects are from **Linux v6.16-rc7**, commit
**`89be9a83`** (July 21, 2025).

## 1. Integrate the checker

Run from the root of this artifact repository, in one shell:

```bash
ARTIFACT_ROOT="$(pwd)"
LLVM_SRC="$ARTIFACT_ROOT/tmp/llvm-project"
LLVM_BUILD="$ARTIFACT_ROOT/tmp/llvm-build-csa"
mkdir -p "$ARTIFACT_ROOT/tmp"
git clone --depth 1 --branch llvmorg-15.0.7 \
  https://github.com/llvm/llvm-project.git "$LLVM_SRC"
cp "$ARTIFACT_ROOT/baseline/KernelRefcountChecker.cpp" \
  "$LLVM_SRC/clang/lib/StaticAnalyzer/Checkers/KernelRefcountChecker.cpp"
```

Make these two small edits in the LLVM checkout:

1. Add `KernelRefcountChecker.cpp` to the source list of
   `add_clang_library(clangStaticAnalyzerCheckers ...)` in
   `clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt`.
2. Append the following definition to
   `clang/include/clang/StaticAnalyzer/Checkers/Checkers.td`, **outside** any
   existing `let ... in { ... }` block:

   ```tablegen
   def KernelRefcountChecker : Checker<"KernelRefcount">,
     ParentPackage<UnixAlpha>,
     HelpText<"Check Linux kernel reference-count balance">,
     Documentation<NotDocumented>;
   ```

The record name `KernelRefcountChecker` matches the source's registration
functions. This definition gives the checker the CLI name
`alpha.unix.KernelRefcount`; copying the source alone does not register it.

## 2. Build Clang

```bash
cmake -S "$LLVM_SRC/llvm" -B "$LLVM_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-15 -DCMAKE_CXX_COMPILER=clang++-15 \
  -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=X86 \
  -DCLANG_ENABLE_STATIC_ANALYZER=ON \
  -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF
cmake --build "$LLVM_BUILD" --target clang clang-extdef-mapping --parallel 4
```

`X86` targets x86/x86-64 hosts; adjust it for another architecture. Use a fresh
build directory and reduce parallelism if memory is limited. This LLVM 15 build
is separate from the LLVM 3.6.2 dependency of the Equinox tools.

## 3. Enable and run the checker

Always use the **newly built** Clang, not the system `clang-15`:

```bash
"$LLVM_BUILD/bin/clang" -cc1 -analyzer-checker-help-alpha | grep KernelRefcount
"$LLVM_BUILD/bin/clang" --analyze --analyzer-no-default-checks \
  --analyzer-output=text \
  -Xanalyzer -analyzer-checker=alpha.unix.KernelRefcount \
  "$ARTIFACT_ROOT/baseline/smoke.c"
```

The smoke test acquires a reference with `fget` and calls `fput` twice. Expect
`Reference count is negative, source: fget, dec: ...`
with checker name `[alpha.unix.KernelRefcount]`; the numeric address suffix is
not stable. No kernel headers, `main`, or linking are needed for this test.
The analyzer runs once and exits; there is no service to start.

For real input, replace `smoke.c` with a C translation unit and supply its normal
compiler flags (include paths, macros, target, etc.). API pairs are compiled
into `IncDecConfig` in the checker source; no runtime configuration or summary
files are required. Leak diagnostics use the message prefix
`Reference count is not zero when object is destroyed`.

## 4. Configure CodeChecker to use our Clang

The paper uses **CSA 15.0.7**, **CodeChecker 6.26.2**, **CTU analysis**, and
**32 analysis workers**. Install that CodeChecker version and explicitly select
the Clang built above, so CodeChecker does not silently use its bundled/system
analyzer:

```bash
python3 -m venv "$ARTIFACT_ROOT/tmp/codechecker-venv"
source "$ARTIFACT_ROOT/tmp/codechecker-venv/bin/activate"
python3 -m pip install 'codechecker==6.26.2'
export CC_ANALYZER_BIN="clangsa:$LLVM_BUILD/bin/clang"
CodeChecker checkers --analyzers clangsa | grep alpha.unix.KernelRefcount
```

`CC_ANALYZER_BIN` requires an **absolute path**. CTU also requires the matching
`clang-extdef-mapping` in the same `bin/` directory; that is why step 2 builds
both targets. If the checker is missing or `--ctu` is unavailable, fix this tool
selection before scanning. The paper does not use a dedicated SMT solver; leave
CodeChecker's Z3 solving and Z3 refutation options disabled (their defaults).

## 5. Scan a kernel codebase with CodeChecker

Use **Linux v6.16-rc7** (commit `89be9a83`), its build dependencies (`make`,
binutils, flex, bison, bc, and the required development libraries).
We used an all-yes configuration for the experiments.

```bash
KERNEL_SRC="$ARTIFACT_ROOT/tmp/linux-6.16-rc7"
git clone --depth 1 --branch v6.16-rc7 \
  https://github.com/torvalds/linux.git "$KERNEL_SRC"
git -C "$KERNEL_SRC" rev-parse HEAD
# Expected: 89be9a83ccf1f88522317ce02f854f30d6115c41
KERNEL_BUILD="$ARTIFACT_ROOT/tmp/kernel-build-csa"
RUN="$ARTIFACT_ROOT/tmp/csa-kernel"
mkdir -p "$KERNEL_BUILD" "$RUN"
make -C "$KERNEL_SRC" O="$KERNEL_BUILD" ARCH=x86_64 \
  CC="$LLVM_BUILD/bin/clang" HOSTCC="$LLVM_BUILD/bin/clang" allyesconfig
CodeChecker log \
  --build "make -C \"$KERNEL_SRC\" O=\"$KERNEL_BUILD\" ARCH=x86_64 CC=\"$LLVM_BUILD/bin/clang\" HOSTCC=\"$LLVM_BUILD/bin/clang\" -j32 vmlinux modules" \
  --output "$RUN/compile_commands.json"
```

The logged build must actually compile the sources. Keep the source tree,
generated headers, and build directory in place for CTU analysis. An existing
Clang-based kernel compilation database can be used instead of the logging
step if it matches this configuration and all recorded paths still exist.

Enable **only our checker** and run the two stages separately, with the paper's
**32 workers** in each stage. The paper distinguishes **T_Parse** (AST parsing)
from **T_Check** (symbolic execution). Use `load-from-pch` explicitly: the
CodeChecker default, `parse-on-demand`, defers imported AST parsing until the
checking stage and does not provide this upfront AST-generation step.

### Parse: generate ASTs and prepare CTU analysis

```bash
/usr/bin/time -p -o "$RUN/parse.time" \
  CodeChecker analyze "$RUN/compile_commands.json" \
  --output "$RUN/reports" --analyzers clangsa \
  --disable-all --enable alpha.unix.KernelRefcount \
  --ctu-collect --ctu-ast-mode load-from-pch --jobs 32
```

`--ctu-collect` generates serialized ASTs and the external-definition mappings
under `$RUN/reports/ctu-dir`; it **does not run the checker**. Preserve this
directory for the next stage. The timing command requires GNU `/usr/bin/time`.

### Check: run reference-count symbolic execution

```bash
/usr/bin/time -p -o "$RUN/check.time" \
  CodeChecker analyze "$RUN/compile_commands.json" \
  --output "$RUN/reports" --analyzers clangsa \
  --disable-all --enable alpha.unix.KernelRefcount \
  --ctu-analyze --ctu-ast-mode load-from-pch --jobs 32
```

`--ctu-analyze` reuses the collected CTU artifacts and runs our checker across
translation units. Use the **same compilation database, output directory,
Clang binary, and AST mode** for both stages; do not change the source,
configuration, or generated headers between them. `--jobs 32` controls workers
in these stages, independently of the build's `make -j32`.

`parse.time` and `check.time` record each command's wall/user/system times.
They are workflow-stage measurements, not isolated compiler-phase timings:
collection also includes CTU mapping and AST serialization, while checking
still parses the main translation units and loads imported ASTs in addition
to symbolic execution. Do not equate these wall times directly with the
paper's T_Parse/T_Check without matching its timing boundaries.

### Inspect the reports

```bash
CodeChecker parse "$RUN/reports"
CodeChecker parse "$RUN/reports" --export html --output "$RUN/html"
```

**`CodeChecker parse` is a report reader/exporter, not the AST Parse stage
above**, and its runtime is not T_Parse. Reports are stored under
`$RUN/reports`; open `$RUN/html/index.html` for HTML results.
`CodeChecker parse` returns exit status **2 when reports are found**, which is
not an analysis failure.
