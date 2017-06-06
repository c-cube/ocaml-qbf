open OUnit2
open Qbf
open Qbf.Formula
open Qbf.QFormula


(*let a,b,c = (Lit.make 1, Lit.make 2, Lit.make 3)*)
let test_quantor _ =
    let a,b,c = (Lit.make 1, Lit.make 2, Lit.make 3) in
    let formula = and_l [atom a; atom b; atom c] in
    let qcnf = QFormula.cnf (forall [a; b] (prop formula)) in
    let _ = solve Quantor.solver qcnf
    in ()


let () = run_test_tt_main (
"quantor">:::[
    "test_quantor">::(test_quantor);
])