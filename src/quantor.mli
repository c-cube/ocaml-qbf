
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

type result =
  | Unknown
  | Sat
  | Unsat
  | Timeout
  | Spaceout

type quantifier =
  | Forall
  | Exists

(** {2 Direct Bindings} *)

module Raw : sig
  type t
  (** Encapsulated solver *)

  val create : unit -> t
  (** Allocate a new QBF solver *)

  val sat : t -> result
  (** Current status of the solver *)

  val scope : t -> quantifier -> unit
  (** Open a new scope with the given kind of quantifier *)

  val add : t -> int -> unit
  (** Add a literal, or end the current clause/scope with [0] *)
end

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
  val cnf : clause list -> t

  val equal : t -> t -> bool
  val compare : t -> t -> int
  val hash : t -> int
  val print : Format.formatter -> t -> unit
end

(** {2 a QBF formula} *)
module Formula : sig
  type t = private
    | Quant of quantifier * Lit.t list * t
    | And of t list
    | Or of t list
    | True
    | False
    | Not of t
    | Atom of Lit.t

  val forall : Lit.t list -> t -> t
  val exists : Lit.t list -> t -> t

  val true_ : t
  val false_ : t
  val and_l : t list-> t
  val or_l : t list -> t
  val atom : Lit.t -> t
  val neg : t -> t

  val equal : t -> t -> bool
  val compare : t -> t -> int
  val hash : t -> int
  val print : Format.formatter -> t -> unit

  val simplify : t -> t
  (** Simplifications *)

  val cnf : t list -> CNF.t
  (** Convert the formula into a prenex-clausal normal form. This can use
      some Tseitin conversion, introducing new literals, to avoid the
      exponential blowup t hat can sometimes occur *)
end

(** {2 Main solving function}

{[
  let cnf = Quantor.CNF.exists [1; 2] (Quantor.CNF.cnf [[1; ~-2]; [2; ~-3]]);;
  Quantor.solve cnf;;
]}
*)

val solve : CNF.t -> result
(** Check whether the CNF formula is true (satisfiable) or false *)
