
module D = Depqbf

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
  ()
