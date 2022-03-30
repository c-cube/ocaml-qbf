
open Printf

let literal (p, i) = if p then Qbf.Lit.make i else Qbf.Lit.neg
      (Qbf.Lit.make i)
let vars = [1;2;3]
let block1 = List.map Qbf.Lit.make vars
let clause1 = List.map literal [(true, 1); (false,2); (false,3)]
let clause2 = List.map literal [(false,1); (true, 2); (true, 3)]
let clause3 = List.map literal [(true, 1); (true, 2); (true, 3)]
let clause4 = List.map literal [(false,1); (false,2); (false,3)]
let formula1 = Qbf.QCNF.exists block1 (Qbf.QCNF.prop [clause1; clause2])
let formula2 = Qbf.QCNF.exists block1 (Qbf.QCNF.prop [clause3; clause4])

let assignment a var =
  let l = Qbf.Lit.make var in
  match a l with
  | Qbf.True  -> Either.Right (true,  var)
  | Qbf.False -> Either.Right (false, var)
  | Qbf.Undef -> Either.Left var

let print_result = function
  | Qbf.Unsat -> failwith "unsat"
  | Qbf.Timeout  -> failwith "timeout in qbf solver"
  | Qbf.Spaceout -> failwith "spaceout in qbf solver"
  | Qbf.Unknown  -> failwith "unknown error in qbf solver"
  | Qbf.Sat a ->
    let undefs, model = List.partition_map (assignment a) vars in
    printf "variables %s have no value assigned\n" (String.concat ", "
                                                       (List.map string_of_int undefs));
    printf "model: %s\n" (String.concat ", " (List.map (fun (b, v) -> (if
                                                                         b then "" else "-") ^ string_of_int v) model))

let main f =
  Format.printf "@[<2>formula: %a@]@." Qbf.QCNF.print f;
  let result = Qbf.solve ~solver:Quantor.solver f in
  print_result result

let () =
  main formula1;
  main formula2

