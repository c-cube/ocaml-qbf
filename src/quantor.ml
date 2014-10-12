
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
      | _ -> failwith (Printf.sprintf "unknown quantor result: %d" i)

  let scope (Quantor q) quant = match quant with
    | Forall -> quantor_scope_forall q
    | Exists -> quantor_scope_exists q

  let add (Quantor q) i = quantor_add q i
    
end
