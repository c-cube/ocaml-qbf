
module D = Depqbf

let add_clause s l =
  List.iter (D.add s) l;
  D.add s 0

let () =
  let s = D.create () in
  let level1 = D.new_scope s Qbf.Forall in
  D.add s 1;
  D.add s 2;
  D.add s 0;
  let _level2 = D.new_scope s Qbf.Exists in
  D.add s 4;
  D.add s 5;
  D.add s 0;
  D.add_var_to_scope s 3 level1;
  D.gc s;
  add_clause s [1;2;3];
  add_clause s [1;-2;3];
  add_clause s [-3];
  let res = D.check s in
  Format.eprintf "res: %a@." Qbf.pp_result res;
  ()
