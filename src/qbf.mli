
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

type 'a printer = Format.formatter -> 'a -> unit

type assignment =
  | True
  | False
  | Undef

val pp_assignment : assignment printer

type quantifier =
  | Forall
  | Exists

val pp_quantifier : quantifier printer

val fresh_int : unit -> int
(** Unique (positive) int generator. Used by {!Formula.cnf} *)

(** {2 a QBF literal} *)
module Lit : sig
  type t = int
  (** A boolean literal is only a non-zero integer *)

  val make : int -> t
  (** Build a literal out of an integer.
      @raise Invalid_argument if the integer is zero *)

  val neg : t -> t
  (** Negation (i.e. opposite) *)

  val to_int : t -> int
  (** The underlying atom, or name (strictly positive integer) *)

  val equal : t -> t -> bool
  val compare : t -> t -> int
  val hash : t -> int
  val print : Format.formatter -> t -> unit
end

(** {2 A QBF Formula in CNF} *)
module CNF : sig
  type clause = Lit.t list

  type t = private
    | Quant of quantifier * Lit.t list * t
    | CNF of clause list

  val forall : Lit.t list -> t -> t
  val exists : Lit.t list -> t -> t
  val quantify : quantifier -> Lit.t list -> t -> t
  val cnf : clause list -> t

  val equal : t -> t -> bool
  val compare : t -> t -> int
  val hash : t -> int

  val print : t printer
  val print_with : pp_lit:Lit.t printer -> t printer
end

(** {2 a QBF formula}

The formula must already be prenex, i.e. it should be nested quantifiers
with a quantifier-free formula inside. *)
module Formula : sig
  type t = private
    | Quant of quantifier * Lit.t list * t
    | Form of form
  and form = private
    | And of form list
    | Or of form list
    | Imply of form * form
    | XOr of form list  (** exactly one element in the list is true *)
    | Equiv of form list (** all the elements are true, or all
                              of them are false *)
    | True
    | False
    | Not of form
    | Atom of Lit.t

  val forall : Lit.t list -> t -> t
  val exists : Lit.t list -> t -> t
  val quantify : quantifier -> Lit.t list -> t -> t
  val form : form -> t

  val true_ : form
  val false_ : form
  val and_l : form list-> form
  val or_l : form list -> form
  val xor_l : form list -> form
  val equiv_l : form list -> form
  val imply : form -> form -> form
  val atom : Lit.t -> form
  val neg : form -> form

  val equal : t -> t -> bool
  val compare : t -> t -> int
  val hash : t -> int

  val print : t printer
  val print_with : pp_lit:Lit.t printer -> t printer

  val simplify : t -> t
  (** Simplifications *)

  val cnf : ?gensym:(unit -> Lit.t) -> t -> CNF.t
  (** Convert the formula into a prenex-clausal normal form. This can use
      some Tseitin conversion, introducing new literals, to avoid the
      exponential blowup that can sometimes occur.
      @param gensym a way to generate new literals to avoid exponential
        blowup. Default is {!fresh_int}. *)
end

(** {2 Solvers} *)

type result =
  | Unknown
  | Sat of (Lit.t -> assignment)
  | Unsat
  | Timeout
  | Spaceout

val pp_result : result printer

type solver = {
  name : string;
  solve : CNF.t -> result;
}

val solve : solver:solver -> CNF.t -> result
(** Check whether the CNF formula is true (satisfiable) or false
    using the given solver *)
