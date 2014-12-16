
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

(** {1 Bindings to DEPQBF} *)

module C = Ctypes
module F = Foreign

type nesting = int
type var_id = int
type constraint_id = int
type lit_id = int  (* signed *)

(* dynamic library to use *)
let from = Dl.dlopen ~filename:"libqdpll.so" ~flags:[Dl.RTLD_LAZY]

(* main type *)
type qdpll
let qdpll : qdpll C.structure C.typ = C.structure "QDPLL"

let qdpll_delete = F.foreign ~from "qdpll_delete" C.(ptr qdpll @-> returning void)

(** {2 Views} *)

(* wrap in a record so that a finalizer can be used *)
type t = {
  s : qdpll C.structure C.ptr;
}

(* use a half-view to access the single field of {!t} *)
let t = C.view
  ~write:(fun {s} -> s)
  ~read:(fun s ->
    let r = {s} in
    Gc.finalise (fun {s} -> qdpll_delete s) r;
    r
  ) (C.ptr qdpll)

let quant = C.view
  ~write:(function
    | Qbf.Exists -> -1
    | Qbf.Forall -> 1
  ) ~read:(function
    | -1 -> Qbf.Exists
    | 1 -> Qbf.Forall
    | 0 -> failwith "quantifier undefined"
    | _ -> assert false
  ) C.int

let assignment = C.view
  ~write:(function
    | Qbf.True -> 1
    | Qbf.False -> -1
    | Qbf.Undef -> 0
  ) ~read:(function
    | 1 -> Qbf.True
    | 0 -> Qbf.Undef
    | -1 -> Qbf.False
    | n -> failwith ("unknown assignment: " ^ string_of_int n)
  ) C.int

(** {2 API} *)

let create = F.foreign ~from "qdpll_create" C.(void @-> returning t)

let configure =
  F.foreign ~from "qdpll_configure" C.(t @-> string @-> returning void)

(* TODO: qdpll_adjust_vars *)

let max_scope_nesting =
  F.foreign ~from "qdpll_get_max_scope_nesting" C.(t @-> returning int)

let push = F.foreign ~from "qdpll_push" C.(t @-> returning void)

let pop = F.foreign ~from "qdpll_pop" C.(t @-> returning int)

let gc = F.foreign ~from "qdpll_gc" C.(t @-> returning void)

let new_scope = F.foreign ~from "qdpll_new_scope" C.(t @-> quant @-> returning int)

let new_scope_at_nesting =
  F.foreign ~from "qdpll_new_scope_at_nesting" C.(t @-> quant @-> int @-> returning int)

let get_value = F.foreign ~from "qdpll_get_value" C.(t @-> int @-> returning assignment)

let add_var_to_scope =
  F.foreign ~from "qdpll_add_var_to_scope" C.(t @-> int @-> int @-> returning void)

(* TODO: qdpll_has_var_active_occs *)

let add = F.foreign ~from "qdpll_add" C.(t @-> int @-> returning void)

let qdpll_sat = F.foreign ~from "qdpll_sat" C.(t @-> returning int)

let sat s = match qdpll_sat s with
  | 0 -> Qbf.Unknown
  | 10 -> Qbf.Sat (get_value s)
  | 20 -> Qbf.Unsat
  | n -> failwith ("unknown depqbf result: " ^ string_of_int n)

let reset = F.foreign ~from "qdpll_reset" C.(t @-> returning void)

let assume = F.foreign ~from "qdpll_assume" C.(t @-> int @-> returning void)

(* TODO: remaining funs *)
