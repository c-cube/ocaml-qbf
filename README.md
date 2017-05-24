# OCaml-QBF

Library to deal with [Quantified Boolean Formulas](https://en.wikipedia.org/wiki/True_quantified_Boolean_formula)
in OCaml.


|        Linux, MacOS        |      Windows       |
| :------------------------: | :----------------: |
| [![Linux and MacOS][1]][2] | [![Windows][3]][4] |

[1]: https://travis-ci.org/maelvalais/ocaml-qbf.svg?branch=master
[2]: https://travis-ci.org/maelvalais/ocaml-qbf
[3]: https://ci.appveyor.com/api/projects/status/d5cjdyqalnlaqxb5?svg=true
[4]: https://ci.appveyor.com/project/maelvalais/ocaml-qbf

## Organization

- The main library, `qbf`, contains types and functions to deal with
  representing boolean literals and quantified formulas, as well as
  a generic interface for solvers.
- A sub-library, `qbf.quantor`, contains a
  binding to the [quantor](http://fmv.jku.at/quantor/) QBF solver. The solver
  itself and [Picosat (version 535)](http://fmv.jku.at/picosat/) are packaged with
  the library for convenience (they are rarely packaged on distributions, and
  require some compilation options such as `-fPIC` to work with OCaml).

## License

The library and its dependencies are licensed under the BSD license
(and the MIT license for picosat), which is fairly permissive.

## Installation

Please use opam (after the first release is done).
