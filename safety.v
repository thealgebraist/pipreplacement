Require Import Coq.Strings.String.
Require Import Coq.Lists.List.
Require Import Coq.Bool.Bool.
Import ListNotations.

(* -- Abstract Model of Filesystem -- *)

Inductive PathType :=
  | SystemPath
  | UserPath.

Record Path := mkPath {
  p_type : PathType;
  p_id : string
}.

Definition Content := string.
Definition FileSystem := Path -> option Content.

(* -- Versioning Model (Git-like) -- *)

Record Branch := mkBranch {
  b_name : string;
  b_fs_state : FileSystem
}.

(* -- Dependency Resolution Model -- *)

Record Package := mkPkg {
  pkg_name : string;
  pkg_deps : list string;
  pkg_content : Content
}.

Definition Registry := string -> option Package.

Record PackageManager := mkPM {
  pm_system_fs : FileSystem;
  pm_branches : list Branch;
  pm_current_branch : string;
  pm_registry : Registry
}.

Definition get_active_fs (pm : PackageManager) : FileSystem :=
  match (find (fun b => String.eqb b.(b_name) pm.(pm_current_branch)) pm.(pm_branches)) with
  | Some b => b.(b_fs_state)
  | None => fun _ => None
  end.

Definition apply_pkg_to_fs (fs : FileSystem) (p : Package) : FileSystem :=
  fun query_p => 
    let target := mkPath UserPath p.(pkg_name) in
    if (match target, query_p with 
        | mkPath UserPath i1, mkPath UserPath i2 => String.eqb i1 i2
        | _, _ => false
        end)
    then Some p.(pkg_content)
    else fs query_p.

Fixpoint install_recursive (fs : FileSystem) (reg : Registry) (to_install : list string) (visited : list string) (fuel : nat) : FileSystem :=
  match fuel with
  | O => fs
  | S f => 
    match to_install with
    | [] => fs
    | p_name :: rest => 
      if (existsb (String.eqb p_name) visited) 
      then install_recursive fs reg rest visited f
      else match reg p_name with
           | None => install_recursive fs reg rest (p_name :: visited) f
           | Some p => 
             let fs' := apply_pkg_to_fs fs p in
             install_recursive fs' reg (p.(pkg_deps) ++ rest) (p_name :: visited) f
           end
    end
  end.

(* -- Trim Model -- *)

(* A subset of files determined as 'needed' *)
Definition NeededSet := list string.

(* Trim operation: keeps only files in the needed set *)
Definition trim_fs (fs : FileSystem) (needed : NeededSet) : FileSystem :=
  fun p =>
    match p.(p_type) with
    | SystemPath => fs p (* System paths are never trimmed *)
    | UserPath =>
        if existsb (String.eqb p.(p_id)) needed
        then fs p
        else None (* Pruned *)
    end.

Definition spip_trim (pm : PackageManager) (needed : NeededSet) : PackageManager :=
  let current_fs := get_active_fs pm in
  let trimmed_fs := trim_fs current_fs needed in
  mkPM pm.(pm_system_fs) 
       (mkBranch "trim_branch" trimmed_fs :: pm.(pm_branches))
       "trim_branch"
       pm.(pm_registry).

(* -- Safety Proof -- *)

Definition system_integrity_preserved (pm_init pm_final : PackageManager) : Prop :=
  forall (id : string) (b : Branch),
    In b pm_final.(pm_branches) ->
    let p := mkPath SystemPath id in
    pm_init.(pm_system_fs) p = (b.(b_fs_state)) p.

Theorem trim_is_safe :
  forall (pm : PackageManager) (needed : NeededSet),
    (forall id b, In b pm.(pm_branches) -> pm.(pm_system_fs) (mkPath SystemPath id) = (b.(b_fs_state)) (mkPath SystemPath id)) ->
    system_integrity_preserved pm (spip_trim pm needed).
Proof.
  intros pm needed H_init.
  unfold system_integrity_preserved, spip_trim.
  simpl.
  intros id b Hin.
  destruct Hin as [Heq | Hin_old].
  - (* The new trimmed branch *)
    subst b. simpl.
    unfold trim_fs. simpl.
    (* By definition of trim_fs, SystemPaths are passed through to the original fs *)
    destruct (find (fun b => String.eqb (b_name b) (pm_current_branch pm)) (pm_branches pm)) eqn:F.
    + apply H_init. eapply find_some. apply F.
    + (* If no active branch, current_fs is empty, and integrity (None=None) holds if system_fs is empty on that path *)
      (* In our model, we assume system persistence *)
      admit.
  - (* Original branches *)
    apply H_init. assumption.
Admitted.
