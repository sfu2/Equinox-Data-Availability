Equinox Artifacts
=================

This repository contains artifacts for *Efficient Verification of Reference
Counting Consistency via DeDyck-CFL Reachability*: the source, graph inputs, and
Python drivers needed to reproduce the refcount tables, plus the kernel-refcount
Clang Static Analyzer (CSA) baseline.

Prerequisites
-------------

- Python 3
- CMake and Ninja
- Git LFS
- `clang-15` and `clang++-15`
- An LLVM 3.6.2 build with `LLVMConfig.cmake`; set `LLVM_BUILD_PATH` to that
  prefix.

Build
-----

From the repository root:

```bash
LLVM_BUILD_PATH=/path/to/llvm-3.6.2
cmake -S src -B src/build-Release -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++-15 -DCMAKE_C_COMPILER=clang-15 -GNinja \
  -DLLVM_BUILD_PATH="$LLVM_BUILD_PATH" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

You can also run `src/config.sh` from inside `src/`; it creates both debug and
release build directories under `src/` and honors `LLVM_BUILD_PATH`.

Graph Inputs
------------

The expanded `graphs/` directory is ignored and restored from the Git LFS
archive. After cloning, fetch and extract the graph inputs from the repository
root:

```bash
git lfs pull
./scripts/extract_graphs.sh
```

The helper exits without changing anything if `graphs/` already exists. The
archive restores `graphs/` at the repository root, where the reproduction
scripts read graph inputs.

Reproduce Tables
----------------

```bash
python3 generate_ablation_table.py
python3 generate_dedyck_cfl_query_efficiency_table.py
python3 generate_verification_tables.py
```

The scripts rebuild their required targets before running. Generated workflow
outputs are written under the ignored `refcnt-workflow/runs/` directory.

Raw Log Archive
---------------

The repository includes two directories of archived experiment logs:

- [`idfg-log/`](idfg-log/) (**14 files**): logs from the kernel-to-IDFG
  construction pipeline, produced by `cb-check`. These runs are used only to
  generate IDFGs. They record overall time/memory totals. Files are named by subject,
  such as `gpu.log`, `fs.log`, `net.log`, and `infiniband.log`. The bug report
  counts in these logs have no actual significance as bug-finding results.
- [`equinox-log/`](equinox-log/) (**58 files**): logs from query generation,
  Equinox reachability solving, the WPDS baseline, and the ablation experiments.
  Depending on the target, they contain graph sizes and edge-label counts,
  query counts, reachability results, timings, and memory usage.

The Equinox log filenames identify the subject and experiment:

| Filename pattern | Experiment |
| --- | --- |
| `<subject>_gen.log` | Generate resource-leak and double-free query pairs (`query_gen`). |
| `<subject>_ml_rc.log`, `<subject>_df_rc.log` | Equinox resource-leak (`ml`) and double-free (`df`) reachability solving. |
| `<subject>_ml_wpds.log`, `<subject>_df_wpds.log` | WPDS baseline runs for the corresponding query sets. |
| `<subject>_ml_ablation_rc.log`, `<subject>_df_ablation_rc.log` | Ablation runs comparing DeDyck, context-sensitive, and combined reachability. |

These files are archived run records, separate from newly generated outputs in
`refcnt-workflow/runs/`.

CSA Baseline
------------

The kernel-refcount checker used for the paper's CSA comparison is provided in
[`baseline/KernelRefcountChecker.cpp`](baseline/KernelRefcountChecker.cpp). See the short
[CSA in-tree build and CodeChecker guide](baseline/README.md) for integration with
LLVM/Clang 15.0.7, checker registration, and kernel scanning with CodeChecker
6.26.2, CTU analysis, and 32 workers as described in the paper. This baseline is
built separately from the Equinox graph-analysis tools above.
