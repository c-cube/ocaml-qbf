# OCaml-QBF

Library to deal with [Quantified Boolean Formulas](https://en.wikipedia.org/wiki/True_quantified_Boolean_formula)
in OCaml.

|                                                        Linux, MacOS, Windows                                                         |
| :----------------------------------------------------------------------------------------------------------------------------------: |
| [![ci](https://github.com/c-cube/ocaml-qbf/workflows/ci/badge.svg)](https://github.com/c-cube/ocaml-qbf/actions?query=workflow%3Aci) |

## Organization

- The main library, `qbf`, contains types and functions to deal with
  representing boolean literals and quantified formulas, as well as
  a generic interface for solvers.
- A sub-library, `qbf.quantor`, contains a
  binding to the [quantor](http://fmv.jku.at/quantor/) QBF solver. The solver
  itself and [Picosat (version 535)](http://fmv.jku.at/picosat/) are packaged with
  the library for convenience (they are rarely packaged on distributions, and
  require some compilation options such as `-fPIC` to work with OCaml).

## Tested configurations

It works with any version of OCaml from 4.08.x to 4.10.x onwards.

1. tested on linux (Ubuntu 16.04, x86_64),
2. tested on MacOS,
3. on windows, the cross-compilation to native win32 under cygwin works fine
   using the mingw64-i686 cross-compiler. It does not work with the x86_64
   compiler[^1]. Should also work under windows-bash for windows 10.

[^1]: This is a bug in quantor's `./configure`. When testing the size of a
      word, windows uses `unsigned long long` but this option is not
      checked (only `unsigned long` and `unsigned`).

## License

The library and its dependencies are licensed under the BSD license
(and the MIT license for picosat), which is fairly permissive.

## Installation

Using opam:

    opam install qbf

From source:

    opam install . --working-dir --deps-only
    dune build

## Known issues

* `process killed by signal -5` is due to a dlopen problem: the binary has been
  linked against the shared library `dllqbf-quantor_stubs.so` but this shared
  lib isn't installed yet.
* `process killed by signal -10` is still unknown. It was happening in
  travis-ci. My workaround was to remove the travis-ci cache (~/.opam was
  cached between two builds).

## Changelog

### 0.3 (unreleased)

- moved to [dune](https://github.com/ocaml/dune) which has become the
  de-facto OCaml build tool.
- fix building ocaml-qbf using opam 2. Opam 2 is sandboxing builds and
  do not accept messing with /tmp
