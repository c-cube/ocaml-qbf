
(*
copyright (c) 2013-2014, simon cruanes
all rights reserved.

redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.  redistributions in binary
form must reproduce the above copyright notice, this list of conditions and the
following disclaimer in the documentation and/or other materials provided with
the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*)

(** {1 Bindings to Quantor} *)

type quantor
(** Abstract type of Quantor solver *)

type result =
  | Unknown
  | Sat
  | Unsat
  | Timeout
  | Spaceout

type quantifier =
  | Forall
  | Exists

external quantor_create : unit -> quantor = "quantor_stub_create"

external quantor_delete : quantor -> unit = "quantor_stub_delete"

external quantor_sat : quantor -> int = "quantor_stub_sat"

external quantor_scope_exists : quantor -> unit = "quantor_stub_exists"

external quantor_scope_forall : quantor -> unit = "quantor_stub_forall"

external quantor_add : quantor -> int -> unit = "quantor_stub_add"

(** {2 Direct Bindings} *)

module Raw = struct
  type t = Quantor of quantor

  let create () =
    let _q = quantor_create () in
    let q = Quantor _q in
    Gc.finalise (fun _ -> quantor_delete _q) q;
    q

  let sat (Quantor q) =
    let i = quantor_sat q in
    match i with
      | 0 -> Unknown
      | 10 -> Sat
      | 20 -> Unsat
      | 30 -> Timeout
      | 40 -> Spaceout
      | _ -> failwith ("unknown quantor result: " ^string_of_int i)

  let scope (Quantor q) quant = match quant with
    | Forall -> quantor_scope_forall q
    | Exists -> quantor_scope_exists q

  let add (Quantor q) i = quantor_add q i
    
end

(** {2 a QBF literal} *)
module Lit = struct
  type t = int
  (** A boolean literal is only a non-zero integer *)

  let make i =
    if i=0 then raise (Invalid_argument "Lit.make");
    i

  let neg i = ~- i
  let atom i = i

  let equal (i:int) j = i=j
  let compare (i:int) j = Pervasives.compare i j
  let hash i = i land max_int
  let print fmt i = Format.fprintf fmt "L%i" i
end

(** {2 A QBF Formula in CNF} *)
module CNF = struct
  type clause = Lit.t list

  type t =
    | Quant of quantifier * Lit.t list * t
    | CNF of clause list

  let forall lits f = match lits, f with
    | [], _ -> f
    | _, Quant (Forall, lits', f') ->
        Quant (Forall, List.rev_append lits lits', f')
    | _ -> Quant (Forall, lits, f)

  let exists lits f = match lits, f with
    | [], _ -> f
    | _, Quant (Exists, lits', f') ->
        Quant (Exists, List.rev_append lits lits', f')
    | _ -> Quant (Exists, lits, f)

  let cnf c = CNF c

  let equal = (=)
  let compare = Pervasives.compare
  let hash = Hashtbl.hash

  let print fmt f = failwith "CNF.print: not implemented" (* TODO *)
end

(** {2 a QBF formula} *)
module Formula = struct
  type t =
    | Quant of quantifier * Lit.t list * t
    | And of t list
    | Or of t list
    | True
    | False
    | Atom of Lit.t

  let true_ = True
  let false_ = False
  let atom l = Atom l

  let rec neg = function
    | Quant (Forall, lits, f) -> Quant (Exists, lits, neg f)
    | Quant (Exists, lits, f) -> Quant (Forall, lits, neg f)
    | Atom l -> Atom (Lit.neg l)
    | And l -> Or (List.map neg l)
    | Or l -> And (List.map neg l)
    | True -> False
    | False -> True

  let forall lits f = match lits, f with
    | [], _ -> f
    | _, Quant (Forall, lits', f') ->
        Quant (Forall, List.rev_append lits lits', f')
    | _ -> Quant (Forall, lits, f)

  let exists lits f = match lits, f with
    | [], _ -> f
    | _, Quant (Exists, lits', f') ->
        Quant (Exists, List.rev_append lits lits', f')
    | _ -> Quant (Exists, lits, f)

  let and_l = function
    | [] -> True
    | [x] -> x
    | l -> And l

  let or_l = function
    | [] -> False
    | [x] -> x
    | l -> Or l

  let equal = (=)
  let compare = Pervasives.compare
  let hash = Hashtbl.hash

  let print fmt f = failwith "Formula.print: not implemented" (* TODO *)

  let cnf f = failwith "Formula.CNF: not implemented" (* TODO *)
end

let rec _add_cnf solver cnf = match cnf with
  | CNF.Quant (quant, lits, cnf') ->
      Raw.scope solver quant;
      List.iter (fun lit -> Raw.add solver lit) lits;
      Raw.add solver 0;
      _add_cnf solver cnf'
  | CNF.CNF clauses ->
      List.iter
        (fun c ->
          List.iter (fun lit -> Raw.add solver lit) c;
          Raw.add solver 0;
        ) clauses

let solve cnf =
  let solver = Raw.create () in
  _add_cnf solver cnf;
  Raw.sat solver
