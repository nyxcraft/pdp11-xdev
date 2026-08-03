# tests/cpp-bsd28 — the 2.8 cpp `defined()` quirk, on record

`defined_quirk.c`/`.expected` capture a genuine 2.8BSD cpp bug: evaluating
`defined(NAME)` leaked `flslvl` state, so a false `#if defined(...)` branch
could swallow subsequent lines.  The merged toolchain ships the 2.9 cpp
revision, which fixes the leak — the live regression is
`tests/cpp/defined_no_leak.c`.

This golden is deliberately **not** wired into `tests/run.sh`: it documents
era behaviour (it would fail against our cpp by design).  It becomes a live
test again only if a bug-compatible 2.8 cpp mode is ever added.
