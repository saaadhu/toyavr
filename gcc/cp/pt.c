/* Handle parameterized types (templates) for GNU C++.
   Copyright (C) 1992, 93, 94, 95, 96, 1997 Free Software Foundation, Inc.
   Written by Ken Raeburn (raeburn@cygnus.com) while at Watchmaker Computing.
   Rewritten by Jason Merrill (jason@cygnus.com).

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* Known bugs or deficiencies include:

     all methods must be provided in header files; can't use a source
     file that contains only the method templates and "just win".  */

#include "config.h"
#include "system.h"
#include "obstack.h"

#include "tree.h"
#include "flags.h"
#include "cp-tree.h"
#include "decl.h"
#include "parse.h"
#include "lex.h"
#include "output.h"
#include "defaults.h"
#include "except.h"
#include "toplev.h"

/* The type of functions taking a tree, and some additional data, and
   returning an int.  */
typedef int (*tree_fn_t) PROTO((tree, void*));

extern struct obstack permanent_obstack;

extern int lineno;
extern char *input_filename;
struct pending_inline *pending_template_expansions;

tree current_template_parms;
HOST_WIDE_INT processing_template_decl;

tree pending_templates;
static tree *template_tail = &pending_templates;

tree maybe_templates;
static tree *maybe_template_tail = &maybe_templates;

int minimal_parse_mode;

int processing_specialization;
int processing_explicit_instantiation;
int processing_template_parmlist;
static int template_header_count;

static tree saved_trees;

#define obstack_chunk_alloc xmalloc
#define obstack_chunk_free free

#define UNIFY_ALLOW_NONE 0
#define UNIFY_ALLOW_MORE_CV_QUAL 1
#define UNIFY_ALLOW_LESS_CV_QUAL 2
#define UNIFY_ALLOW_DERIVED 4

static int unify PROTO((tree, tree, tree, tree, int, int*));
static void add_pending_template PROTO((tree));
static int push_tinst_level PROTO((tree));
static tree classtype_mangled_name PROTO((tree));
static char *mangle_class_name_for_template PROTO((char *, tree, tree));
static tree tsubst_expr_values PROTO((tree, tree));
static int list_eq PROTO((tree, tree));
static tree get_class_bindings PROTO((tree, tree, tree));
static tree coerce_template_parms PROTO((tree, tree, tree, int, int));
static tree tsubst_enum	PROTO((tree, tree));
static tree add_to_template_args PROTO((tree, tree));
static tree add_outermost_template_args PROTO((tree, tree));
static void maybe_adjust_types_for_deduction PROTO((unification_kind_t, tree*,
						    tree*)); 
static int  type_unification_real PROTO((tree, tree, tree, tree,
					 int, unification_kind_t, int, int*));
static void note_template_header PROTO((int));
static tree maybe_fold_nontype_arg PROTO((tree));
static tree convert_nontype_argument PROTO((tree, tree));
static tree get_bindings_overload PROTO((tree, tree, tree));
static int for_each_template_parm PROTO((tree, tree_fn_t, void*));
static tree build_template_parm_index PROTO((int, int, int, tree, tree));
static int inline_needs_template_parms PROTO((tree));
static void push_inline_template_parms_recursive PROTO((tree, int));
static tree retrieve_specialization PROTO((tree, tree));
static tree register_specialization PROTO((tree, tree, tree));
static int unregister_specialization PROTO((tree, tree));
static void print_candidates PROTO((tree));
static tree reduce_template_parm_level PROTO((tree, tree, int));
static tree build_template_decl PROTO((tree, tree));
static int mark_template_parm PROTO((tree, void *));
static tree tsubst_friend_function PROTO((tree, tree));
static tree tsubst_friend_class PROTO((tree, tree));
static tree get_bindings_real PROTO((tree, tree, tree, int));
static int template_decl_level PROTO((tree));
static tree maybe_get_template_decl_from_type_decl PROTO((tree));
static int check_cv_quals_for_unify PROTO((int, tree, tree));
static tree tsubst_template_arg_vector PROTO((tree, tree));
static tree tsubst_template_parms PROTO((tree, tree));
static void regenerate_decl_from_template PROTO((tree, tree));
static int is_member_template_class PROTO((tree));
static tree most_specialized PROTO((tree, tree, tree));
static tree most_specialized_class PROTO((tree, tree));
static tree most_general_template PROTO((tree));
static void set_mangled_name_for_template_decl PROTO((tree));
static int template_class_depth_real PROTO((tree, int));
static tree tsubst_aggr_type PROTO((tree, tree, tree, int));
static tree tsubst_decl PROTO((tree, tree, tree, tree));

/* We use TREE_VECs to hold template arguments.  If there is only one
   level of template arguments, then the TREE_VEC contains the
   arguments directly.  If there is more than one level of template
   arguments, then each entry in the TREE_VEC is itself a TREE_VEC,
   containing the template arguments for a single level.  The first
   entry in the outer TREE_VEC is the outermost level of template
   parameters; the last is the innermost.  

   It is incorrect to ever form a template argument vector containing
   only one level of arguments, but which is a TREE_VEC containing as
   its only entry the TREE_VEC for that level.  */

/* Non-zero if the template arguments is actually a vector of vectors,
   rather than just a vector.  */
#define TMPL_ARGS_HAVE_MULTIPLE_LEVELS(NODE) \
  (NODE != NULL_TREE						\
   && TREE_CODE (NODE) == TREE_VEC				\
   && TREE_VEC_LENGTH (NODE) > 0				\
   && TREE_VEC_ELT (NODE, 0) != NULL_TREE			\
   && TREE_CODE (TREE_VEC_ELT (NODE, 0)) == TREE_VEC)

/* The depth of a template argument vector.  When called directly by
   the parser, we use a TREE_LIST rather than a TREE_VEC to represent
   template arguments.  In fact, we may even see NULL_TREE if there
   are no template arguments.  In both of those cases, there is only
   one level of template arguments.  */
#define TMPL_ARGS_DEPTH(NODE)					\
  (TMPL_ARGS_HAVE_MULTIPLE_LEVELS (NODE) ? TREE_VEC_LENGTH (NODE) : 1)

/* The LEVELth level of the template ARGS.  Note that template
   parameter levels are indexed from 1, not from 0.  */
#define TMPL_ARGS_LEVEL(ARGS, LEVEL)		\
  (TMPL_ARGS_HAVE_MULTIPLE_LEVELS (ARGS) 	\
   ? TREE_VEC_ELT ((ARGS), (LEVEL) - 1) : ARGS)

/* Set the LEVELth level of the template ARGS to VAL.  This macro does
   not work with single-level argument vectors.  */
#define SET_TMPL_ARGS_LEVEL(ARGS, LEVEL, VAL)	\
  (TREE_VEC_ELT ((ARGS), (LEVEL) - 1) = (VAL))

/* Accesses the IDXth parameter in the LEVELth level of the ARGS.  */
#define TMPL_ARG(ARGS, LEVEL, IDX)				\
  (TREE_VEC_ELT (TMPL_ARGS_LEVEL (ARGS, LEVEL), IDX))

/* Set the IDXth element in the LEVELth level of ARGS to VAL.  This
   macro does not work with single-level argument vectors.  */
#define SET_TMPL_ARG(ARGS, LEVEL, IDX, VAL)			\
  (TREE_VEC_ELT (TREE_VEC_ELT ((ARGS), (LEVEL) - 1), (IDX)) = (VAL))

/* Given a single level of template arguments in NODE, return the
   number of arguments.  */
#define NUM_TMPL_ARGS(NODE) 				\
  ((NODE) == NULL_TREE ? 0 				\
   : (TREE_CODE (NODE) == TREE_VEC 			\
      ? TREE_VEC_LENGTH (NODE) : list_length (NODE)))

/* The number of levels of template parameters given by NODE.  */
#define TMPL_PARMS_DEPTH(NODE) \
  (TREE_INT_CST_HIGH (TREE_PURPOSE (NODE)))

/* Do any processing required when DECL (a member template declaration
   using TEMPLATE_PARAMETERS as its innermost parameter list) is
   finished.  Returns the TEMPLATE_DECL corresponding to DECL, unless
   it is a specialization, in which case the DECL itself is returned.  */

tree
finish_member_template_decl (template_parameters, decl)
  tree template_parameters;
  tree decl;
{
  finish_template_decl (template_parameters);

  if (decl == NULL_TREE || decl == void_type_node)
    return NULL_TREE;
  else if (TREE_CODE (decl) == TREE_LIST)
    {
      /* Assume that the class is the only declspec.  */
      decl = TREE_VALUE (decl);
      if (IS_AGGR_TYPE (decl) && CLASSTYPE_TEMPLATE_INFO (decl)
	  && ! CLASSTYPE_TEMPLATE_SPECIALIZATION (decl))
	{
	  tree tmpl = CLASSTYPE_TI_TEMPLATE (decl);
	  check_member_template (tmpl);
	  return tmpl;
	}
      return NULL_TREE;
    }
  else if (DECL_TEMPLATE_INFO (decl))
    {
      if (!DECL_TEMPLATE_SPECIALIZATION (decl))
	{
	  check_member_template (DECL_TI_TEMPLATE (decl));
	  return DECL_TI_TEMPLATE (decl);
	}
      else
	return decl;
    } 
  else
    cp_error ("invalid member template declaration `%D'", decl);
	

  return error_mark_node;
}

/* Returns the template nesting level of the indicated class TYPE.
   
   For example, in:
     template <class T>
     struct A
     {
       template <class U>
       struct B {};
     };

   A<T>::B<U> has depth two, while A<T> has depth one.  
   Both A<T>::B<int> and A<int>::B<U> have depth one, if
   COUNT_SPECIALIZATIONS is 0 or if they are instantiations, not
   specializations.  

   This function is guaranteed to return 0 if passed NULL_TREE so
   that, for example, `template_class_depth (current_class_type)' is
   always safe.  */

int 
template_class_depth_real (type, count_specializations)
     tree type;
     int count_specializations;
{
  int depth;

  for (depth = 0; 
       type && TREE_CODE (type) != NAMESPACE_DECL;
       type = (TREE_CODE (type) == FUNCTION_DECL) 
	 ? DECL_REAL_CONTEXT (type) : TYPE_CONTEXT (type))
    {
      if (TREE_CODE (type) != FUNCTION_DECL)
	{
	  if (CLASSTYPE_TEMPLATE_INFO (type)
	      && PRIMARY_TEMPLATE_P (CLASSTYPE_TI_TEMPLATE (type))
	      && ((count_specializations
		   && CLASSTYPE_TEMPLATE_SPECIALIZATION (type))
		  || uses_template_parms (CLASSTYPE_TI_ARGS (type))))
	    ++depth;
	}
      else 
	{
	  if (DECL_TEMPLATE_INFO (type)
	      && PRIMARY_TEMPLATE_P (DECL_TI_TEMPLATE (type))
	      && ((count_specializations
		   && DECL_TEMPLATE_SPECIALIZATION (type))
		  || uses_template_parms (DECL_TI_ARGS (type))))
	    ++depth;
	}
    }

  return depth;
}

/* Returns the template nesting level of the indicated class TYPE.
   Like template_class_depth_real, but instantiations do not count in
   the depth.  */

int 
template_class_depth (type)
     tree type;
{
  return template_class_depth_real (type, /*count_specializations=*/0);
}

/* Returns 1 if processing DECL as part of do_pending_inlines
   needs us to push template parms.  */

static int
inline_needs_template_parms (decl)
     tree decl;
{
  if (! DECL_TEMPLATE_INFO (decl))
    return 0;

  return (TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (most_general_template (decl)))
	  > (processing_template_decl + DECL_TEMPLATE_SPECIALIZATION (decl)));
}

/* Subroutine of maybe_begin_member_template_processing.
   Push the template parms in PARMS, starting from LEVELS steps into the
   chain, and ending at the beginning, since template parms are listed
   innermost first.  */

static void
push_inline_template_parms_recursive (parmlist, levels)
     tree parmlist;
     int levels;
{
  tree parms = TREE_VALUE (parmlist);
  int i;

  if (levels > 1)
    push_inline_template_parms_recursive (TREE_CHAIN (parmlist), levels - 1);

  ++processing_template_decl;
  current_template_parms
    = tree_cons (build_int_2 (0, processing_template_decl),
		 parms, current_template_parms);
  TEMPLATE_PARMS_FOR_INLINE (current_template_parms) = 1;

  pushlevel (0);
  for (i = 0; i < TREE_VEC_LENGTH (parms); ++i) 
    {
      tree parm = TREE_VALUE (TREE_VEC_ELT (parms, i));
      my_friendly_assert (TREE_CODE_CLASS (TREE_CODE (parm)) == 'd', 0);

      switch (TREE_CODE (parm))
	{
	case TYPE_DECL:
	case TEMPLATE_DECL:
	  pushdecl (parm);
	  break;

	case PARM_DECL:
	  {
	    /* Make a CONST_DECL as is done in process_template_parm. */
	    tree decl = build_decl (CONST_DECL, DECL_NAME (parm),
				    TREE_TYPE (parm));
	    DECL_INITIAL (decl) = DECL_INITIAL (parm);
	    pushdecl (decl);
	  }
	  break;

	default:
	  my_friendly_abort (0);
	}
    }
}

/* Restore the template parameter context for a member template or
   a friend template defined in a class definition.  */

void
maybe_begin_member_template_processing (decl)
     tree decl;
{
  tree parms;
  int levels;

  if (! inline_needs_template_parms (decl))
    return;

  parms = DECL_TEMPLATE_PARMS (most_general_template (decl));

  levels = TMPL_PARMS_DEPTH (parms) - processing_template_decl;

  if (DECL_TEMPLATE_SPECIALIZATION (decl))
    {
      --levels;
      parms = TREE_CHAIN (parms);
    }

  push_inline_template_parms_recursive (parms, levels);
}

/* Undo the effects of begin_member_template_processing. */

void 
maybe_end_member_template_processing (decl)
     tree decl;
{
  if (! processing_template_decl)
    return;

  while (current_template_parms
	 && TEMPLATE_PARMS_FOR_INLINE (current_template_parms))
    {
      --processing_template_decl;
      current_template_parms = TREE_CHAIN (current_template_parms);
      poplevel (0, 0, 0);
    }
}

/* Returns non-zero iff T is a member template function.  We must be
   careful as in

     template <class T> class C { void f(); }

   Here, f is a template function, and a member, but not a member
   template.  This function does not concern itself with the origin of
   T, only its present state.  So if we have 

     template <class T> class C { template <class U> void f(U); }

   then neither C<int>::f<char> nor C<T>::f<double> is considered
   to be a member template.  */

int
is_member_template (t)
     tree t;
{
  if (TREE_CODE (t) != FUNCTION_DECL
      && !DECL_FUNCTION_TEMPLATE_P (t))
    /* Anything that isn't a function or a template function is
       certainly not a member template.  */
    return 0;

  /* A local class can't have member templates.  */
  if (hack_decl_function_context (t))
    return 0;

  if ((DECL_FUNCTION_MEMBER_P (t) 
       && !DECL_TEMPLATE_SPECIALIZATION (t))
      || (TREE_CODE (t) == TEMPLATE_DECL 
	  && DECL_FUNCTION_MEMBER_P (DECL_TEMPLATE_RESULT (t))))
    {
      tree tmpl;

      if (DECL_FUNCTION_TEMPLATE_P (t))
	tmpl = t;
      else if (DECL_TEMPLATE_INFO (t) 
	       && DECL_FUNCTION_TEMPLATE_P (DECL_TI_TEMPLATE (t)))
	tmpl = DECL_TI_TEMPLATE (t);
      else
	tmpl = NULL_TREE;

      if (tmpl
	  /* If there are more levels of template parameters than
	     there are template classes surrounding the declaration,
	     then we have a member template.  */
	  && (TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (tmpl)) > 
	      template_class_depth (DECL_CLASS_CONTEXT (t))))
	return 1;
    }

  return 0;
}

/* Returns non-zero iff T is a member template class.  See
   is_member_template for a description of what precisely constitutes
   a member template.  */

int
is_member_template_class (t)
     tree t;
{
  if (!DECL_CLASS_TEMPLATE_P (t))
    /* Anything that isn't a class template, is certainly not a member
       template.  */
    return 0;

  if (!DECL_CLASS_SCOPE_P (t))
    /* Anything whose context isn't a class type is surely not a
       member template.  */
    return 0;

  /* If there are more levels of template parameters than there are
     template classes surrounding the declaration, then we have a
     member template.  */
  return  (TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (t)) > 
	   template_class_depth (DECL_CONTEXT (t)));
}

/* Return a new template argument vector which contains all of ARGS,
   but has as its innermost set of arguments the EXTRA_ARGS.  The
   resulting vector will be built on a temporary obstack, and so must
   be explicitly copied to the permanent obstack, if required.  */

static tree
add_to_template_args (args, extra_args)
     tree args;
     tree extra_args;
{
  tree new_args;
  int extra_depth;
  int i;
  int j;

  extra_depth = TMPL_ARGS_DEPTH (extra_args);
  new_args = make_temp_vec (TMPL_ARGS_DEPTH (args) + extra_depth);

  for (i = 1; i <= TMPL_ARGS_DEPTH (args); ++i)
    SET_TMPL_ARGS_LEVEL (new_args, i, TMPL_ARGS_LEVEL (args, i));

  for (j = 1; j <= extra_depth; ++j, ++i)
    SET_TMPL_ARGS_LEVEL (new_args, i, TMPL_ARGS_LEVEL (extra_args, j));
    
  return new_args;
}

/* Like add_to_template_args, but only the outermost ARGS are added to
   the EXTRA_ARGS.  In particular, all but TMPL_ARGS_DEPTH
   (EXTRA_ARGS) levels are added.  This function is used to combine
   the template arguments from a partial instantiation with the
   template arguments used to attain the full instantiation from the
   partial instantiation.  */

static tree
add_outermost_template_args (args, extra_args)
     tree args;
     tree extra_args;
{
  tree new_args;

  /* If there are more levels of EXTRA_ARGS than there are ARGS,
     something very fishy is going on.  */
  my_friendly_assert (TMPL_ARGS_DEPTH (args) >= TMPL_ARGS_DEPTH (extra_args),
		      0);

  /* If *all* the new arguments will be the EXTRA_ARGS, just return
     them.  */
  if (TMPL_ARGS_DEPTH (args) == TMPL_ARGS_DEPTH (extra_args))
    return extra_args;

  /* For the moment, we make ARGS look like it contains fewer levels.  */
  TREE_VEC_LENGTH (args) -= TMPL_ARGS_DEPTH (extra_args);
  
  new_args = add_to_template_args (args, extra_args);

  /* Now, we restore ARGS to its full dimensions.  */
  TREE_VEC_LENGTH (args) += TMPL_ARGS_DEPTH (extra_args);

  return new_args;
}

/* We've got a template header coming up; push to a new level for storing
   the parms.  */

void
begin_template_parm_list ()
{
  /* We use a non-tag-transparent scope here, which causes pushtag to
     put tags in this scope, rather than in the enclosing class or
     namespace scope.  This is the right thing, since we want
     TEMPLATE_DECLS, and not TYPE_DECLS for template classes.  For a
     global template class, push_template_decl handles putting the
     TEMPLATE_DECL into top-level scope.  For a nested template class,
     e.g.:

       template <class T> struct S1 {
         template <class T> struct S2 {}; 
       };

     pushtag contains special code to call pushdecl_with_scope on the
     TEMPLATE_DECL for S2.  */
  pushlevel (0);
  declare_pseudo_global_level ();
  ++processing_template_decl;
  ++processing_template_parmlist;
  note_template_header (0);
}

/* We've just seen template <>. */

void
begin_specialization ()
{
  note_template_header (1);
}

/* Called at then end of processing a declaration preceeded by
   template<>.  */

void 
end_specialization ()
{
  reset_specialization ();
}

/* Any template <>'s that we have seen thus far are not referring to a
   function specialization. */

void
reset_specialization ()
{
  processing_specialization = 0;
  template_header_count = 0;
}

/* We've just seen a template header.  If SPECIALIZATION is non-zero,
   it was of the form template <>.  */

static void 
note_template_header (specialization)
     int specialization;
{
  processing_specialization = specialization;
  template_header_count++;
}

/* We're beginning an explicit instantiation.  */

void
begin_explicit_instantiation ()
{
  ++processing_explicit_instantiation;
}


void
end_explicit_instantiation ()
{
  my_friendly_assert(processing_explicit_instantiation > 0, 0);
  --processing_explicit_instantiation;
}

/* The TYPE is being declared.  If it is a template type, that means it
   is a partial specialization.  Do appropriate error-checking.  */

void 
maybe_process_partial_specialization (type)
     tree type;
{
  if (IS_AGGR_TYPE (type) && CLASSTYPE_USE_TEMPLATE (type))
    {
      if (CLASSTYPE_IMPLICIT_INSTANTIATION (type)
	  && TYPE_SIZE (type) == NULL_TREE)
	{
	  SET_CLASSTYPE_TEMPLATE_SPECIALIZATION (type);
	  if (processing_template_decl)
	    push_template_decl (TYPE_MAIN_DECL (type));
	}
      else if (CLASSTYPE_TEMPLATE_INSTANTIATION (type))
	cp_error ("specialization of `%T' after instantiation", type);
    }
}

/* Retrieve the specialization (in the sense of [temp.spec] - a
   specialization is either an instantiation or an explicit
   specialization) of TMPL for the given template ARGS.  If there is
   no such specialization, return NULL_TREE.  The ARGS are a vector of
   arguments, or a vector of vectors of arguments, in the case of
   templates with more than one level of parameters.  */
   
static tree
retrieve_specialization (tmpl, args)
     tree tmpl;
     tree args;
{
  tree s;

  my_friendly_assert (TREE_CODE (tmpl) == TEMPLATE_DECL, 0);

  /* There should be as many levels of arguments as there are
     levels of parameters.  */
  my_friendly_assert (TMPL_ARGS_DEPTH (args) 
		      == TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (tmpl)),
		      0);
		      
  for (s = DECL_TEMPLATE_SPECIALIZATIONS (tmpl);
       s != NULL_TREE;
       s = TREE_CHAIN (s))
    if (comp_template_args (TREE_PURPOSE (s), args))
      return TREE_VALUE (s);

  return NULL_TREE;
}

/* Returns non-zero iff DECL is a specialization of TMPL.  */

int
is_specialization_of (decl, tmpl)
     tree decl;
     tree tmpl;
{
  tree t;

  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      for (t = decl; 
	   t != NULL_TREE;
	   t = DECL_TEMPLATE_INFO (t) ? DECL_TI_TEMPLATE (t) : NULL_TREE)
	if (t == tmpl)
	  return 1;
    }
  else 
    {
      my_friendly_assert (TREE_CODE (decl) == TYPE_DECL, 0);

      for (t = TREE_TYPE (decl);
	   t != NULL_TREE;
	   t = CLASSTYPE_USE_TEMPLATE (t)
	     ? TREE_TYPE (CLASSTYPE_TI_TEMPLATE (t)) : NULL_TREE)
	if (comptypes (TYPE_MAIN_VARIANT (t), 
		       TYPE_MAIN_VARIANT (TREE_TYPE (tmpl)), 1))
	  return 1;
    }  

  return 0;
}

/* Register the specialization SPEC as a specialization of TMPL with
   the indicated ARGS.  Returns SPEC, or an equivalent prior
   declaration, if available.  */

static tree
register_specialization (spec, tmpl, args)
     tree spec;
     tree tmpl;
     tree args;
{
  tree s;

  my_friendly_assert (TREE_CODE (tmpl) == TEMPLATE_DECL, 0);

  if (TREE_CODE (spec) == FUNCTION_DECL 
      && uses_template_parms (DECL_TI_ARGS (spec)))
    /* This is the FUNCTION_DECL for a partial instantiation.  Don't
       register it; we want the corresponding TEMPLATE_DECL instead.
       We use `uses_template_parms (DECL_TI_ARGS (spec))' rather than
       the more obvious `uses_template_parms (spec)' to avoid problems
       with default function arguments.  In particular, given
       something like this:

          template <class T> void f(T t1, T t = T())

       the default argument expression is not substituted for in an
       instantiation unless and until it is actually needed.  */
    return spec;
    
  /* There should be as many levels of arguments as there are
     levels of parameters.  */
  my_friendly_assert (TMPL_ARGS_DEPTH (args) 
		      == TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (tmpl)),
		      0);

  for (s = DECL_TEMPLATE_SPECIALIZATIONS (tmpl);
       s != NULL_TREE;
       s = TREE_CHAIN (s))
    if (comp_template_args (TREE_PURPOSE (s), args))
      {
	tree fn = TREE_VALUE (s);

	if (DECL_TEMPLATE_SPECIALIZATION (spec))
	  {
	    if (DECL_TEMPLATE_INSTANTIATION (fn))
	      {
		if (TREE_USED (fn) 
		    || DECL_EXPLICIT_INSTANTIATION (fn))
		  {
		    cp_error ("specialization of %D after instantiation",
			      fn);
		    return spec;
		  }
		else
		  {
		    /* This situation should occur only if the first
		       specialization is an implicit instantiation,
		       the second is an explicit specialization, and
		       the implicit instantiation has not yet been
		       used.  That situation can occur if we have
		       implicitly instantiated a member function and
		       then specialized it later.

		       We can also wind up here if a friend
		       declaration that looked like an instantiation
		       turns out to be a specialization:

		         template <class T> void foo(T);
			 class S { friend void foo<>(int) };
			 template <> void foo(int);  

		       We transform the existing DECL in place so that
		       any pointers to it become pointers to the
		       updated declaration.  */
		    duplicate_decls (spec, TREE_VALUE (s));
		    return TREE_VALUE (s);
		  }
	      }
	    else if (DECL_TEMPLATE_SPECIALIZATION (fn))
	      {
		duplicate_decls (spec, TREE_VALUE (s));
		return TREE_VALUE (s);
	      }
	  }
      }

  DECL_TEMPLATE_SPECIALIZATIONS (tmpl)
     = perm_tree_cons (args, spec, DECL_TEMPLATE_SPECIALIZATIONS (tmpl));

  return spec;
}

/* Unregister the specialization SPEC as a specialization of TMPL.
   Returns nonzero if the SPEC was listed as a specialization of
   TMPL.  */

static int
unregister_specialization (spec, tmpl)
     tree spec;
     tree tmpl;
{
  tree* s;

  for (s = &DECL_TEMPLATE_SPECIALIZATIONS (tmpl);
       *s != NULL_TREE;
       s = &TREE_CHAIN (*s))
    if (TREE_VALUE (*s) == spec)
      {
	*s = TREE_CHAIN (*s);
	return 1;
      }

  return 0;
}

/* Print the list of candidate FNS in an error message.  */

static void
print_candidates (fns)
     tree fns;
{
  tree fn;

  char* str = "candidates are:";

  for (fn = fns; fn != NULL_TREE; fn = TREE_CHAIN (fn))
    {
      cp_error_at ("%s %+#D", str, TREE_VALUE (fn));
      str = "               ";
    }
}

/* Returns the template (one of the functions given by TEMPLATE_ID)
   which can be specialized to match the indicated DECL with the
   explicit template args given in TEMPLATE_ID.  If
   NEED_MEMBER_TEMPLATE is true the function is a specialization of a
   member template.  The template args (those explicitly specified and
   those deduced) are output in a newly created vector *TARGS_OUT.  If
   it is impossible to determine the result, an error message is
   issued, unless COMPLAIN is 0.  The DECL may be NULL_TREE if none is
   available.  */

tree
determine_specialization (template_id, decl, targs_out, 
			  need_member_template,
			  complain)
     tree template_id;
     tree decl;
     tree* targs_out;
     int need_member_template;
     int complain;
{
  tree fns, targs_in;
  tree templates = NULL_TREE;
  tree fn;
  int i;

  *targs_out = NULL_TREE;

  if (template_id == error_mark_node)
    return error_mark_node;

  fns = TREE_OPERAND (template_id, 0);
  targs_in = TREE_OPERAND (template_id, 1);

  if (fns == error_mark_node)
    return error_mark_node;

  /* Check for baselinks. */
  if (TREE_CODE (fns) == TREE_LIST)
    fns = TREE_VALUE (fns);

  for (; fns; fns = OVL_NEXT (fns))
    {
      tree tmpl;

      fn = OVL_CURRENT (fns);
      if (!need_member_template 
	  && TREE_CODE (fn) == FUNCTION_DECL 
	  && DECL_FUNCTION_MEMBER_P (fn)
	  && DECL_USE_TEMPLATE (fn)
	  && DECL_TI_TEMPLATE (fn))
	/* We can get here when processing something like:
	     template <class T> class X { void f(); }
	     template <> void X<int>::f() {}
	   We're specializing a member function, but not a member
	   template.  */
	tmpl = DECL_TI_TEMPLATE (fn);
      else if (TREE_CODE (fn) != TEMPLATE_DECL
	       || (need_member_template && !is_member_template (fn)))
	continue;
      else
	tmpl = fn;

      if (list_length (targs_in) > DECL_NTPARMS (tmpl))
	continue;

      if (decl == NULL_TREE)
	{
	  tree targs = make_scratch_vec (DECL_NTPARMS (tmpl));

	  /* We allow incomplete unification here, because we are going to
	     check all the functions. */
	  i = type_unification (DECL_INNERMOST_TEMPLATE_PARMS (tmpl),
				targs,
				NULL_TREE,
				NULL_TREE,  
				targs_in,
				DEDUCE_EXACT, 1);
      
	  if (i == 0) 
	    /* Unification was successful.  */
	    templates = scratch_tree_cons (targs, tmpl, templates);
	}
      else
	templates = scratch_tree_cons (NULL_TREE, tmpl, templates);
    }
  
  if (decl != NULL_TREE)
    {
      tree tmpl = most_specialized (templates, decl, targs_in);
      tree inner_args;
      tree tmpl_args;

      if (tmpl == error_mark_node) 
	goto ambiguous;
      else if (tmpl == NULL_TREE)
	goto no_match;

      inner_args = get_bindings (tmpl, decl, targs_in);
      tmpl_args = DECL_TI_ARGS (DECL_RESULT (tmpl));
      if (TMPL_ARGS_HAVE_MULTIPLE_LEVELS (tmpl_args))
	{
	  *targs_out = copy_node (tmpl_args);
	  SET_TMPL_ARGS_LEVEL (*targs_out, 
			       TMPL_ARGS_DEPTH (*targs_out),
			       inner_args);
	}
      else
	*targs_out = inner_args;
      
      return tmpl;
    }

  if (templates == NULL_TREE)
    {
    no_match:
      if (complain)
	{
	  cp_error_at ("template-id `%D' for `%+D' does not match any template declaration",
		       template_id, decl);
	  return error_mark_node;
	}
      return NULL_TREE;
    }
  else if (TREE_CHAIN (templates) != NULL_TREE) 
    {
    ambiguous:
      if (complain)
	{
	  cp_error_at ("ambiguous template specialization `%D' for `%+D'",
		       template_id, decl);
	  print_candidates (templates);
	  return error_mark_node;
	}
      return NULL_TREE;
    }

  /* We have one, and exactly one, match. */
  *targs_out = TREE_PURPOSE (templates);
  return TREE_VALUE (templates);
}
      
/* Check to see if the function just declared, as indicated in
   DECLARATOR, and in DECL, is a specialization of a function
   template.  We may also discover that the declaration is an explicit
   instantiation at this point.

   Returns DECL, or an equivalent declaration that should be used
   instead. 
   
   FLAGS is a bitmask consisting of the following flags: 

   1: We are being called by finish_struct.  (We are unable to
      determine what template is specialized by an in-class
      declaration until the class definition is complete, so
      finish_struct_methods calls this function again later to finish
      the job.)
   2: The function has a definition.
   4: The function is a friend.
   8: The function is known to be a specialization of a member
      template. 

   The TEMPLATE_COUNT is the number of references to qualifying
   template classes that appeared in the name of the function.  For
   example, in

     template <class T> struct S { void f(); };
     void S<int>::f();
     
   the TEMPLATE_COUNT would be 1.  However, explicitly specialized
   classes are not counted in the TEMPLATE_COUNT, so that in

     template <class T> struct S {};
     template <> struct S<int> { void f(); }
     template <> void S<int>::f();

   the TEMPLATE_COUNT would be 0.  (Note that this declaration is
   illegal; there should be no template <>.)

   If the function is a specialization, it is marked as such via
   DECL_TEMPLATE_SPECIALIZATION.  Furthermore, its DECL_TEMPLATE_INFO
   is set up correctly, and it is added to the list of specializations 
   for that template.  */

tree
check_explicit_specialization (declarator, decl, template_count, flags)
     tree declarator;
     tree decl;
     int template_count;
     int flags;
{
  int finish_member = flags & 1;
  int have_def = flags & 2;
  int is_friend = flags & 4;
  int specialization = 0;
  int explicit_instantiation = 0;
  int member_specialization = flags & 8;

  tree ctype = DECL_CLASS_CONTEXT (decl);
  tree dname = DECL_NAME (decl);

  if (!finish_member)
    {
      if (processing_specialization) 
	{
	  /* The last template header was of the form template <>.  */
	  
	  if (template_header_count > template_count) 
	    {
	      /* There were more template headers than qualifying template
		 classes.  */
	      if (template_header_count - template_count > 1)
		/* There shouldn't be that many template parameter
		   lists.  There can be at most one parameter list for
		   every qualifying class, plus one for the function
		   itself.  */
		cp_error ("too many template parameter lists in declaration of `%D'", decl);

	      SET_DECL_TEMPLATE_SPECIALIZATION (decl);
	      if (ctype)
		member_specialization = 1;
	      else
		specialization = 1;
	    }
	  else if (template_header_count == template_count)
	    {
	      /* The counts are equal.  So, this might be a
		 specialization, but it is not a specialization of a
		 member template.  It might be something like
		 
		 template <class T> struct S { 
	         void f(int i); 
		 };
		 template <>
		 void S<int>::f(int i) {}  */
	      specialization = 1;
	      SET_DECL_TEMPLATE_SPECIALIZATION (decl);
	    }
	  else 
	    {
	      /* This cannot be an explicit specialization.  There are not
		 enough headers for all of the qualifying classes.  For
		 example, we might have:
	     
		 template <>
		 void S<int>::T<char>::f();

		 But, we're missing another template <>.  */
	      cp_error("too few template parameter lists in declaration of `%D'", decl);
	      return decl;
	    } 
	}
      else if (processing_explicit_instantiation)
	{
	  if (template_header_count)
	    cp_error ("template parameter list used in explicit instantiation");
	  
	  if (have_def)
	    cp_error ("definition provided for explicit instantiation");

	  explicit_instantiation = 1;
	}
      else if (ctype != NULL_TREE
	       && !TYPE_BEING_DEFINED (ctype)
	       && CLASSTYPE_TEMPLATE_INSTANTIATION (ctype)
	       && !is_friend)
	{
	  /* This case catches outdated code that looks like this:

	     template <class T> struct S { void f(); };
	     void S<int>::f() {} // Missing template <>

	     We disable this check when the type is being defined to
	     avoid complaining about default compiler-generated
	     constructors, destructors, and assignment operators.
	     Since the type is an instantiation, not a specialization,
	     these are the only functions that can be defined before
	     the class is complete.  */

	  /* If they said
	       template <class T> void S<int>::f() {}
	     that's bogus.  */
	  if (template_header_count)
	    {
	      cp_error ("template parameters specified in specialization");
	      return decl;
	    }

	  if (pedantic)
	    cp_pedwarn
	      ("explicit specialization not preceded by `template <>'");
	  specialization = 1;
	  SET_DECL_TEMPLATE_SPECIALIZATION (decl);
	}
      else if (TREE_CODE (declarator) == TEMPLATE_ID_EXPR)
	{
	  if (is_friend)
	    /* This could be something like:

	         template <class T> void f(T);
		 class S { friend void f<>(int); }  */
	    specialization = 1;
	  else
	    {
	      /* This case handles bogus declarations like template <>
		 template <class T> void f<int>(); */

	      cp_error ("template-id `%D' in declaration of primary template",
			declarator);
	      return decl;
	    }
	}
    }

  if (specialization || member_specialization)
    {
      tree t = TYPE_ARG_TYPES (TREE_TYPE (decl));
      for (; t; t = TREE_CHAIN (t))
	if (TREE_PURPOSE (t))
	  {
	    cp_pedwarn
	      ("default argument specified in explicit specialization");
	    break;
	  }
    }

  if (specialization || member_specialization || explicit_instantiation)
    {
      tree gen_tmpl;
      tree tmpl = NULL_TREE;
      tree targs = NULL_TREE;

      /* Make sure that the declarator is a TEMPLATE_ID_EXPR.  */
      if (TREE_CODE (declarator) != TEMPLATE_ID_EXPR)
	{
	  tree fns;

	  my_friendly_assert (TREE_CODE (declarator) == IDENTIFIER_NODE, 
			      0);
	  if (!ctype)
	    fns = IDENTIFIER_NAMESPACE_VALUE (dname);
	  else
	    fns = dname;

	  declarator = 
	    lookup_template_function (fns, NULL_TREE);
	}

      if (declarator == error_mark_node)
	return error_mark_node;

      if (TREE_CODE (TREE_OPERAND (declarator, 0)) == LOOKUP_EXPR)
	{
	  /* A friend declaration.  We can't do much, because we don't
	   know what this resolves to, yet.  */
	  my_friendly_assert (is_friend != 0, 0);
	  my_friendly_assert (!explicit_instantiation, 0);
	  SET_DECL_IMPLICIT_INSTANTIATION (decl);
	  return decl;
	} 

      if (ctype != NULL_TREE && TYPE_BEING_DEFINED (ctype))
	{
	  if (!explicit_instantiation)
	    {
	      /* Since finish_struct_1 has not been called yet, we
		 can't call lookup_fnfields.  We note that this
		 template is a specialization, and proceed, letting
		 finish_struct fix this up later.  */
	      tree ti = perm_tree_cons (NULL_TREE, 
					TREE_OPERAND (declarator, 1),
					NULL_TREE);
	      TI_PENDING_SPECIALIZATION_FLAG (ti) = 1;
	      DECL_TEMPLATE_INFO (decl) = ti;
	    }
	  else
	    /* It's not legal to write an explicit instantiation in
	       class scope, e.g.:

	         class C { template void f(); }

	       This case is caught by the parser.  However, on
	       something like:
	       
	         template class C { void f(); };

	       (which is illegal) we can get here.  The error will be
	       issued later.  */
	    ;

	  return decl;
	}
      else if (ctype != NULL_TREE 
	       && (TREE_CODE (TREE_OPERAND (declarator, 0)) ==
		   IDENTIFIER_NODE))
	{
	  /* Find the list of functions in ctype that have the same
	     name as the declared function.  */
	  tree name = TREE_OPERAND (declarator, 0);
	  tree fns;
	  
	  if (name == constructor_name (ctype) 
	      || name == constructor_name_full (ctype))
	    {
	      int is_constructor = DECL_CONSTRUCTOR_P (decl);
	      
	      if (is_constructor ? !TYPE_HAS_CONSTRUCTOR (ctype)
		  : !TYPE_HAS_DESTRUCTOR (ctype))
		{
		  /* From [temp.expl.spec]:
		       
		     If such an explicit specialization for the member
		     of a class template names an implicitly-declared
		     special member function (clause _special_), the
		     program is ill-formed.  

		     Similar language is found in [temp.explicit].  */
		  cp_error ("specialization of implicitly-declared special member function");

		  return decl;
		}

	      name = is_constructor ? ctor_identifier : dtor_identifier;
	    }

	  fns = lookup_fnfields (TYPE_BINFO (ctype), name, 1);
	  
	  if (fns == NULL_TREE) 
	    {
	      cp_error ("no member function `%s' declared in `%T'",
			IDENTIFIER_POINTER (name),
			ctype);
	      return decl;
	    }
	  else
	    TREE_OPERAND (declarator, 0) = fns;
	}
      
      /* Figure out what exactly is being specialized at this point.
	 Note that for an explicit instantiation, even one for a
	 member function, we cannot tell apriori whether the
	 instantiation is for a member template, or just a member
	 function of a template class.  Even if a member template is
	 being instantiated, the member template arguments may be
	 elided if they can be deduced from the rest of the
	 declaration.  */
      tmpl = determine_specialization (declarator, decl,
				       &targs, 
				       member_specialization,
				       1);
	    
      if (tmpl && tmpl != error_mark_node)
	{
	  gen_tmpl = most_general_template (tmpl);

	  if (explicit_instantiation)
	    {
	      decl = instantiate_template (tmpl, innermost_args (targs));
	      if (!DECL_TEMPLATE_SPECIALIZATION (decl))
		/* There doesn't seem to be anything in the draft to
		   prevent a specialization from being explicitly
		   instantiated.  We're careful not to destroy the
		   information indicating that this is a
		   specialization here.  */
		SET_DECL_EXPLICIT_INSTANTIATION (decl);
	      return decl;
	    }
	  
	  /* If we though that the DECL was a member function, but it
	     turns out to be specializing a static member function,
	     make DECL a static member function as well.  */
	  if (DECL_STATIC_FUNCTION_P (tmpl)
	      && DECL_NONSTATIC_MEMBER_FUNCTION_P (decl))
	    {
	      revert_static_member_fn (&decl, 0, 0);
	      last_function_parms = TREE_CHAIN (last_function_parms);
	    }

	  /* Set up the DECL_TEMPLATE_INFO for DECL.  */
	  DECL_TEMPLATE_INFO (decl) 
	    = perm_tree_cons (tmpl, targs, NULL_TREE);

	  /* Mangle the function name appropriately.  Note that we do
	     not mangle specializations of non-template member
	     functions of template classes, e.g. with

	       template <class T> struct S { void f(); }

	     and given the specialization 

	       template <> void S<int>::f() {}

	     we do not mangle S<int>::f() here.  That's because it's
	     just an ordinary member function and doesn't need special
	     treatment.  We do this here so that the ordinary,
	     non-template, name-mangling algorith will not be used
	     later.  */
	  if ((is_member_template (tmpl) || ctype == NULL_TREE)
	      && name_mangling_version >= 1)
	    set_mangled_name_for_template_decl (decl);

	  if (is_friend && !have_def)
	    /* This is not really a declaration of a specialization.
	       It's just the name of an instantiation.  But, it's not
	       a request for an instantiation, either.  */
	      SET_DECL_IMPLICIT_INSTANTIATION (decl);

	  /* Register this specialization so that we can find it
	     again.  */
	  decl = register_specialization (decl, gen_tmpl, targs);

	  return decl;
	}
    }
  
  return decl;
}

/* TYPE is being declared.  Verify that the use of template headers
   and such is reasonable.  Issue error messages if not.  */

void
maybe_check_template_type (type)
     tree type;
{
  if (template_header_count)
    {
      /* We are in the scope of some `template <...>' header.  */

      int context_depth 
	= template_class_depth_real (TYPE_CONTEXT (type),
				     /*count_specializations=*/1);

      if (template_header_count <= context_depth)
	/* This is OK; the template headers are for the context.  We
	   are actually too lenient here; like
	   check_explicit_specialization we should consider the number
	   of template types included in the actual declaration.  For
	   example, 

	     template <class T> struct S {
	       template <class U> template <class V>
	       struct I {};
	     }; 

	   is illegal, but:

	     template <class T> struct S {
	       template <class U> struct I;
	     }; 

	     template <class T> template <class U.
	     struct S<T>::I {};

	   is not.  */
	; 
      else if (template_header_count > context_depth + 1)
	/* There are two many template parameter lists.  */
	cp_error ("too many template parameter lists in declaration of `%T'", type); 
    }
}

/* Returns 1 iff PARMS1 and PARMS2 are identical sets of template
   parameters.  These are represented in the same format used for
   DECL_TEMPLATE_PARMS.  */

int comp_template_parms (parms1, parms2)
     tree parms1;
     tree parms2;
{
  tree p1;
  tree p2;

  if (parms1 == parms2)
    return 1;

  for (p1 = parms1, p2 = parms2; 
       p1 != NULL_TREE && p2 != NULL_TREE;
       p1 = TREE_CHAIN (p1), p2 = TREE_CHAIN (p2))
    {
      tree t1 = TREE_VALUE (p1);
      tree t2 = TREE_VALUE (p2);
      int i;

      my_friendly_assert (TREE_CODE (t1) == TREE_VEC, 0);
      my_friendly_assert (TREE_CODE (t2) == TREE_VEC, 0);

      if (TREE_VEC_LENGTH (t1) != TREE_VEC_LENGTH (t2))
	return 0;

      for (i = 0; i < TREE_VEC_LENGTH (t2); ++i) 
	{
	  tree parm1 = TREE_VALUE (TREE_VEC_ELT (t1, i));
	  tree parm2 = TREE_VALUE (TREE_VEC_ELT (t2, i));

	  if (TREE_CODE (parm1) != TREE_CODE (parm2))
	    return 0;

	  if (TREE_CODE (parm1) == TEMPLATE_TYPE_PARM)
	    continue;
	  else if (!comptypes (TREE_TYPE (parm1), 
			       TREE_TYPE (parm2), 1))
	    return 0;
	}
    }

  if ((p1 != NULL_TREE) != (p2 != NULL_TREE))
    /* One set of parameters has more parameters lists than the
       other.  */
    return 0;

  return 1;
}

/* Return a new TEMPLATE_PARM_INDEX with the indicated INDEX, LEVEL,
   ORIG_LEVEL, DECL, and TYPE.  */

static tree
build_template_parm_index (index, level, orig_level, decl, type)
     int index;
     int level;
     int orig_level;
     tree decl;
     tree type;
{
  tree t = make_node (TEMPLATE_PARM_INDEX);
  TEMPLATE_PARM_IDX (t) = index;
  TEMPLATE_PARM_LEVEL (t) = level;
  TEMPLATE_PARM_ORIG_LEVEL (t) = orig_level;
  TEMPLATE_PARM_DECL (t) = decl;
  TREE_TYPE (t) = type;

  return t;
}

/* Return a TEMPLATE_PARM_INDEX, similar to INDEX, but whose
   TEMPLATE_PARM_LEVEL has been decreased by LEVELS.  If such a
   TEMPLATE_PARM_INDEX already exists, it is returned; otherwise, a
   new one is created.  */

static tree 
reduce_template_parm_level (index, type, levels)
     tree index;
     tree type;
     int levels;
{
  if (TEMPLATE_PARM_DESCENDANTS (index) == NULL_TREE
      || (TEMPLATE_PARM_LEVEL (TEMPLATE_PARM_DESCENDANTS (index))
	  != TEMPLATE_PARM_LEVEL (index) - levels))
    {
      tree decl 
	= build_decl (TREE_CODE (TEMPLATE_PARM_DECL (index)),
		      DECL_NAME (TEMPLATE_PARM_DECL (index)),
		      type);
      tree t
	= build_template_parm_index (TEMPLATE_PARM_IDX (index),
				     TEMPLATE_PARM_LEVEL (index) - levels,
				     TEMPLATE_PARM_ORIG_LEVEL (index),
				     decl, type);
      TEMPLATE_PARM_DESCENDANTS (index) = t;

      /* Template template parameters need this.  */
      DECL_TEMPLATE_PARMS (decl)
	= DECL_TEMPLATE_PARMS (TEMPLATE_PARM_DECL (index));
    }

  return TEMPLATE_PARM_DESCENDANTS (index);
}

/* Process information from new template parameter NEXT and append it to the
   LIST being built.  */

tree
process_template_parm (list, next)
     tree list, next;
{
  tree parm;
  tree decl = 0;
  tree defval;
  int is_type, idx;

  parm = next;
  my_friendly_assert (TREE_CODE (parm) == TREE_LIST, 259);
  defval = TREE_PURPOSE (parm);
  parm = TREE_VALUE (parm);
  is_type = TREE_PURPOSE (parm) == class_type_node;

  if (list)
    {
      tree p = TREE_VALUE (tree_last (list));

      if (TREE_CODE (p) == TYPE_DECL)
	idx = TEMPLATE_TYPE_IDX (TREE_TYPE (p));
      else if (TREE_CODE (p) == TEMPLATE_DECL)
	idx = TEMPLATE_TYPE_IDX (TREE_TYPE (DECL_TEMPLATE_RESULT (p)));
      else
	idx = TEMPLATE_PARM_IDX (DECL_INITIAL (p));
      ++idx;
    }
  else
    idx = 0;

  if (!is_type)
    {
      my_friendly_assert (TREE_CODE (TREE_PURPOSE (parm)) == TREE_LIST, 260);
      /* is a const-param */
      parm = grokdeclarator (TREE_VALUE (parm), TREE_PURPOSE (parm),
			     PARM, 0, NULL_TREE);
      /* A template parameter is not modifiable.  */
      TREE_READONLY (parm) = 1;
      if (IS_AGGR_TYPE (TREE_TYPE (parm))
	  && TREE_CODE (TREE_TYPE (parm)) != TEMPLATE_TYPE_PARM
	  && TREE_CODE (TREE_TYPE (parm)) != TYPENAME_TYPE)
	{
	  cp_error ("`%#T' is not a valid type for a template constant parameter",
		    TREE_TYPE (parm));
	  if (DECL_NAME (parm) == NULL_TREE)
	    error ("  a template type parameter must begin with `class' or `typename'");
	  TREE_TYPE (parm) = void_type_node;
	}
      else if (pedantic
	       && (TREE_CODE (TREE_TYPE (parm)) == REAL_TYPE
		   || TREE_CODE (TREE_TYPE (parm)) == COMPLEX_TYPE))
	cp_pedwarn ("`%T' is not a valid type for a template constant parameter",
		    TREE_TYPE (parm));
      if (TREE_PERMANENT (parm) == 0)
        {
	  parm = copy_node (parm);
	  TREE_PERMANENT (parm) = 1;
        }
      decl = build_decl (CONST_DECL, DECL_NAME (parm), TREE_TYPE (parm));
      DECL_INITIAL (parm) = DECL_INITIAL (decl) 
	= build_template_parm_index (idx, processing_template_decl,
				     processing_template_decl,
				     decl, TREE_TYPE (parm));
    }
  else
    {
      tree t;
      parm = TREE_VALUE (parm);
      
      if (parm && TREE_CODE (parm) == TEMPLATE_DECL)
	{
	  t = make_lang_type (TEMPLATE_TEMPLATE_PARM);
	  /* This is for distinguishing between real templates and template 
	     template parameters */
	  TREE_TYPE (parm) = t;
	  TREE_TYPE (DECL_TEMPLATE_RESULT (parm)) = t;
	  decl = parm;
	}
      else
	{
	  t = make_lang_type (TEMPLATE_TYPE_PARM);
	  /* parm is either IDENTIFIER_NODE or NULL_TREE */
	  decl = build_decl (TYPE_DECL, parm, t);
	}
        
      CLASSTYPE_GOT_SEMICOLON (t) = 1;
      TYPE_NAME (t) = decl;
      TYPE_STUB_DECL (t) = decl;
      parm = decl;
      TEMPLATE_TYPE_PARM_INDEX (t)
	= build_template_parm_index (idx, processing_template_decl, 
				     processing_template_decl,
				     decl, TREE_TYPE (parm));
    }
  SET_DECL_ARTIFICIAL (decl);
  pushdecl (decl);
  parm = build_tree_list (defval, parm);
  return chainon (list, parm);
}

/* The end of a template parameter list has been reached.  Process the
   tree list into a parameter vector, converting each parameter into a more
   useful form.	 Type parameters are saved as IDENTIFIER_NODEs, and others
   as PARM_DECLs.  */

tree
end_template_parm_list (parms)
     tree parms;
{
  int nparms;
  tree parm;
  tree saved_parmlist = make_tree_vec (list_length (parms));

  current_template_parms
    = tree_cons (build_int_2 (0, processing_template_decl),
		 saved_parmlist, current_template_parms);

  for (parm = parms, nparms = 0; parm; parm = TREE_CHAIN (parm), nparms++)
    TREE_VEC_ELT (saved_parmlist, nparms) = parm;

  --processing_template_parmlist;

  return saved_parmlist;
}

/* end_template_decl is called after a template declaration is seen.  */

void
end_template_decl ()
{
  reset_specialization ();

  if (! processing_template_decl)
    return;

  /* This matches the pushlevel in begin_template_parm_list.  */
  poplevel (0, 0, 0);

  --processing_template_decl;
  current_template_parms = TREE_CHAIN (current_template_parms);
  (void) get_pending_sizes ();	/* Why? */
}

/* Given a template argument vector containing the template PARMS.
   The innermost PARMS are given first.  */

tree
current_template_args ()
{
  tree header;
  tree args;
  int length = TMPL_PARMS_DEPTH (current_template_parms);
  int l = length;

  /* If there is only one level of template parameters, we do not
     create a TREE_VEC of TREE_VECs.  Instead, we return a single
     TREE_VEC containing the arguments.  */
  if (length > 1)
    args = make_tree_vec (length);

  for (header = current_template_parms; header; header = TREE_CHAIN (header))
    {
      tree a = copy_node (TREE_VALUE (header));
      int i;

      TREE_TYPE (a) = NULL_TREE;
      for (i = TREE_VEC_LENGTH (a) - 1; i >= 0; --i)
	{
	  tree t = TREE_VEC_ELT (a, i);

	  /* T will be a list if we are called from within a
	     begin/end_template_parm_list pair, but a vector directly
	     if within a begin/end_member_template_processing pair.  */
	  if (TREE_CODE (t) == TREE_LIST) 
	    {
	      t = TREE_VALUE (t);
	      
	      if (TREE_CODE (t) == TYPE_DECL 
		  || TREE_CODE (t) == TEMPLATE_DECL)
		t = TREE_TYPE (t);
	      else
		t = DECL_INITIAL (t);
	      TREE_VEC_ELT (a, i) = t;
	    }
	}

      if (length > 1)
	TREE_VEC_ELT (args, --l) = a;
      else
	args = a;
    }

  return args;
}

/* Return a TEMPLATE_DECL corresponding to DECL, using the indicated
   template PARMS.  Used by push_template_decl below.  */

static tree
build_template_decl (decl, parms)
     tree decl;
     tree parms;
{
  tree tmpl = build_lang_decl (TEMPLATE_DECL, DECL_NAME (decl), NULL_TREE);
  DECL_TEMPLATE_PARMS (tmpl) = parms;
  DECL_CONTEXT (tmpl) = DECL_CONTEXT (decl);
  if (DECL_LANG_SPECIFIC (decl))
    {
      DECL_CLASS_CONTEXT (tmpl) = DECL_CLASS_CONTEXT (decl);
      DECL_STATIC_FUNCTION_P (tmpl) = 
	DECL_STATIC_FUNCTION_P (decl);
    }

  return tmpl;
}

struct template_parm_data
{
  int level;
  int* parms;
};

/* Subroutine of push_template_decl used to see if each template
   parameter in a partial specialization is used in the explicit
   argument list.  If T is of the LEVEL given in DATA (which is
   treated as a template_parm_data*), then DATA->PARMS is marked
   appropriately.  */

static int
mark_template_parm (t, data)
     tree t;
     void* data;
{
  int level;
  int idx;
  struct template_parm_data* tpd = (struct template_parm_data*) data;

  if (TREE_CODE (t) == TEMPLATE_PARM_INDEX)
    {
      level = TEMPLATE_PARM_LEVEL (t);
      idx = TEMPLATE_PARM_IDX (t);
    }
  else
    {
      level = TEMPLATE_TYPE_LEVEL (t);
      idx = TEMPLATE_TYPE_IDX (t);
    }

  if (level == tpd->level)
    tpd->parms[idx] = 1;

  /* Return zero so that for_each_template_parm will continue the
     traversal of the tree; we want to mark *every* template parm.  */
  return 0;
}

/* Creates a TEMPLATE_DECL for the indicated DECL using the template
   parameters given by current_template_args, or reuses a
   previously existing one, if appropriate.  Returns the DECL, or an
   equivalent one, if it is replaced via a call to duplicate_decls.  

   If IS_FRIEND is non-zero, DECL is a friend declaration.  */

tree
push_template_decl_real (decl, is_friend)
     tree decl;
     int is_friend;
{
  tree tmpl;
  tree args;
  tree info;
  tree ctx;
  int primary;

  is_friend |= (TREE_CODE (decl) == FUNCTION_DECL && DECL_FRIEND_P (decl));

  if (is_friend)
    /* For a friend, we want the context of the friend function, not
       the type of which it is a friend.  */
    ctx = DECL_CONTEXT (decl);
  else if (DECL_REAL_CONTEXT (decl)
	   && TREE_CODE (DECL_REAL_CONTEXT (decl)) != NAMESPACE_DECL)
    /* In the case of a virtual function, we want the class in which
       it is defined.  */
    ctx = DECL_REAL_CONTEXT (decl);
  else
    /* Otherwise, if we're currently definining some class, the DECL
       is assumed to be a member of the class.  */
    ctx = current_class_type;

  if (ctx && TREE_CODE (ctx) == NAMESPACE_DECL)
    ctx = NULL_TREE;

  if (!DECL_CONTEXT (decl))
    DECL_CONTEXT (decl) = FROB_CONTEXT (current_namespace);

  /* For determining whether this is a primary template or not, we're really
     interested in the lexical context, not the true context.  */
  if (is_friend)
    info = current_class_type;
  else
    info = ctx;

  if (info && TREE_CODE (info) == FUNCTION_DECL)
    primary = 0;
  /* Note that template_class_depth returns 0 if given NULL_TREE, so
     this next line works even when we are at global scope.  */
  else if (processing_template_decl > template_class_depth (info))
    primary = 1;
  else
    primary = 0;

  if (primary)
    {
      if (current_lang_name == lang_name_c)
	cp_error ("template with C linkage");
      if (TREE_CODE (decl) == TYPE_DECL && ANON_AGGRNAME_P (DECL_NAME (decl)))
	cp_error ("template class without a name");
      if (TREE_CODE (decl) == TYPE_DECL 
	  && TREE_CODE (TREE_TYPE (decl)) == ENUMERAL_TYPE)
	cp_error ("template declaration of `%#T'", TREE_TYPE (decl));
    }

  /* Partial specialization.  */
  if (TREE_CODE (decl) == TYPE_DECL && DECL_ARTIFICIAL (decl)
      && TREE_CODE (TREE_TYPE (decl)) != ENUMERAL_TYPE
      && CLASSTYPE_TEMPLATE_SPECIALIZATION (TREE_TYPE (decl)))
    {
      tree type = TREE_TYPE (decl);
      tree maintmpl = CLASSTYPE_TI_TEMPLATE (type);
      tree specargs = CLASSTYPE_TI_ARGS (type);

      /* We check that each of the template parameters given in the
	 partial specialization is used in the argument list to the
	 specialization.  For example:
	 
	   template <class T> struct S;
	   template <class T> struct S<T*>;

	 The second declaration is OK because `T*' uses the template
	 parameter T, whereas
       
           template <class T> struct S<int>;

	 is no good.  Even trickier is:

	   template <class T>
	   struct S1
	   {
	      template <class U>
	      struct S2;
	      template <class U>
	      struct S2<T>;
	   };
	   
	 The S2<T> declaration is actually illegal; it is a
	 full-specialization.  Of course, 

              template <class U>
              struct S2<T (*)(U)>;

         or some such would have been OK.  */
      int  i;
      struct template_parm_data tpd;
      int ntparms 
	= TREE_VEC_LENGTH (INNERMOST_TEMPLATE_PARMS (current_template_parms));
      int did_error_intro = 0;

      tpd.level = TMPL_PARMS_DEPTH (current_template_parms);
      tpd.parms = alloca (sizeof (int) * ntparms);
      for (i = 0; i < ntparms; ++i)
	tpd.parms[i] = 0;
      for (i = 0; i < TREE_VEC_LENGTH (specargs); ++i)
	for_each_template_parm (TREE_VEC_ELT (specargs, i),
				&mark_template_parm,
				&tpd);
      for (i = 0; i < ntparms; ++i)
	if (tpd.parms[i] == 0)
	  {
	    /* One of the template parms was not used in the
	       specialization.  */
	    if (!did_error_intro)
	      {
		cp_error ("template parameters not used in partial specialization:");
		did_error_intro = 1;
	      }

	    cp_error ("        `%D'", 
		      TREE_VALUE (TREE_VEC_ELT 
				  (TREE_VALUE (current_template_parms),
				   i)));
	  }

      /* [temp.class.spec]

	 The argument list of the specialization shall not be
	 identical to the implicit argument list of the primary
	 template.  */
      if (comp_template_args (specargs, 
			      CLASSTYPE_TI_ARGS (TREE_TYPE (maintmpl))))
	cp_error ("partial specialization `%T' does not specialize any template arguments", type);
			   
      if (retrieve_specialization (maintmpl, specargs))
	/* We've already got this specialization.  */
	return decl;

      DECL_TEMPLATE_SPECIALIZATIONS (maintmpl) = CLASSTYPE_TI_SPEC_INFO (type)
	= perm_tree_cons (innermost_args (specargs),
			  INNERMOST_TEMPLATE_PARMS (current_template_parms),
			  DECL_TEMPLATE_SPECIALIZATIONS (maintmpl));
      TREE_TYPE (DECL_TEMPLATE_SPECIALIZATIONS (maintmpl)) = type;
      return decl;
    }

  args = current_template_args ();

  if (!ctx 
      || TREE_CODE (ctx) == FUNCTION_DECL
      || TYPE_BEING_DEFINED (ctx)
      || (is_friend && !DECL_TEMPLATE_INFO (decl)))
    {
      if (DECL_LANG_SPECIFIC (decl)
	  && DECL_TEMPLATE_INFO (decl)
	  && DECL_TI_TEMPLATE (decl))
	tmpl = DECL_TI_TEMPLATE (decl);
      else
	{
	  tmpl = build_template_decl (decl, current_template_parms);
	  
	  if (DECL_LANG_SPECIFIC (decl)
	      && DECL_TEMPLATE_SPECIALIZATION (decl))
	    {
	      /* A specialization of a member template of a template
		 class. */
	      SET_DECL_TEMPLATE_SPECIALIZATION (tmpl);
	      DECL_TEMPLATE_INFO (tmpl) = DECL_TEMPLATE_INFO (decl);
	      DECL_TEMPLATE_INFO (decl) = NULL_TREE;
	    }
	}
    }
  else
    {
      tree t;
      tree a;

      if (CLASSTYPE_TEMPLATE_INSTANTIATION (ctx))
	cp_error ("must specialize `%#T' before defining member `%#D'",
		  ctx, decl);
      if (TREE_CODE (decl) == TYPE_DECL)
	{
	  if ((IS_AGGR_TYPE_CODE (TREE_CODE (TREE_TYPE (decl)))
	       || TREE_CODE (TREE_TYPE (decl)) == ENUMERAL_TYPE)
	      && TYPE_TEMPLATE_INFO (TREE_TYPE (decl))
	      && TYPE_TI_TEMPLATE (TREE_TYPE (decl)))
	    tmpl = TYPE_TI_TEMPLATE (TREE_TYPE (decl));
	  else
	    {
	      cp_error ("`%D' does not declare a template type", decl);
	      return decl;
	    }
	}
      else if (! DECL_TEMPLATE_INFO (decl))
	{
	  cp_error ("template definition of non-template `%#D'", decl);
	  return decl;
	}
      else
	tmpl = DECL_TI_TEMPLATE (decl);
      
      if (is_member_template (tmpl) || is_member_template_class (tmpl))
	{
	  if (DECL_FUNCTION_TEMPLATE_P (tmpl)
	      && DECL_TEMPLATE_INFO (decl) && DECL_TI_ARGS (decl) 
	      && DECL_TEMPLATE_SPECIALIZATION (decl))
	    {
	      tree new_tmpl;

	      /* The declaration is a specialization of a member
		 template, declared outside the class.  Therefore, the
		 innermost template arguments will be NULL, so we
		 replace them with the arguments determined by the
		 earlier call to check_explicit_specialization.  */
	      args = DECL_TI_ARGS (decl);

	      new_tmpl 
		= build_template_decl (decl, current_template_parms);
	      DECL_TEMPLATE_RESULT (new_tmpl) = decl;
	      TREE_TYPE (new_tmpl) = TREE_TYPE (decl);
	      DECL_TI_TEMPLATE (decl) = new_tmpl;
	      SET_DECL_TEMPLATE_SPECIALIZATION (new_tmpl);
	      DECL_TEMPLATE_INFO (new_tmpl) = 
		perm_tree_cons (tmpl, args, NULL_TREE);

	      register_specialization (new_tmpl, tmpl, args);
	      return decl;
	    }
	  
	  a = innermost_args (args);
	  t = DECL_INNERMOST_TEMPLATE_PARMS (tmpl);
	  if (TREE_VEC_LENGTH (t) != TREE_VEC_LENGTH (a))
	    {
	      cp_error ("got %d template parameters for `%#D'",
			TREE_VEC_LENGTH (a), decl);
	      cp_error ("  but %d required", TREE_VEC_LENGTH (t));
	    }
	  if (TMPL_ARGS_DEPTH (args) > 1)
	    /* Get the template parameters for the enclosing template
	       class.  */ 
	    a = TMPL_ARGS_LEVEL (args, TMPL_ARGS_DEPTH (args) - 1);
	  else
	    a = NULL_TREE;
	}
      else 
	a = innermost_args (args);

      t = NULL_TREE;

      if (CLASSTYPE_TEMPLATE_SPECIALIZATION (ctx))
	{
	  /* When processing an inline member template of a
	     specialized class, there is no CLASSTYPE_TI_SPEC_INFO.  */
	  if (CLASSTYPE_TI_SPEC_INFO (ctx))
	    t = TREE_VALUE (CLASSTYPE_TI_SPEC_INFO (ctx));
	}
      else if (CLASSTYPE_TEMPLATE_INFO (ctx))
	t = DECL_INNERMOST_TEMPLATE_PARMS (CLASSTYPE_TI_TEMPLATE (ctx));

      /* There should be template arguments if and only if there is a
	 template class.  */
      my_friendly_assert((a != NULL_TREE) == (t != NULL_TREE), 0);

      if (t != NULL_TREE 
	  && TREE_VEC_LENGTH (t) != TREE_VEC_LENGTH (a))
	{
	  cp_error ("got %d template parameters for `%#D'",
		    TREE_VEC_LENGTH (a), decl);
	  cp_error ("  but `%#T' has %d", ctx, TREE_VEC_LENGTH (t));
	}
    }

  DECL_TEMPLATE_RESULT (tmpl) = decl;
  TREE_TYPE (tmpl) = TREE_TYPE (decl);

  /* Push template declarations for global functions and types.  Note
     that we do not try to push a global template friend declared in a
     template class; such a thing may well depend on the template
     parameters of the class.  */
  if (! ctx 
      && !(is_friend && template_class_depth (current_class_type) > 0))
    tmpl = pushdecl_namespace_level (tmpl);

  if (primary)
    DECL_PRIMARY_TEMPLATE (tmpl) = tmpl;

  info = perm_tree_cons (tmpl, args, NULL_TREE);

  if (TREE_CODE (decl) == TYPE_DECL && DECL_ARTIFICIAL (decl))
    {
      SET_TYPE_TEMPLATE_INFO (TREE_TYPE (tmpl), info);
      if ((!ctx || TREE_CODE (ctx) != FUNCTION_DECL)
	  && TREE_CODE (TREE_TYPE (decl)) != ENUMERAL_TYPE)
	DECL_NAME (decl) = classtype_mangled_name (TREE_TYPE (decl));
    }
  else if (! DECL_LANG_SPECIFIC (decl))
    cp_error ("template declaration of `%#D'", decl);
  else
    DECL_TEMPLATE_INFO (decl) = info;

  return DECL_TEMPLATE_RESULT (tmpl);
}

tree
push_template_decl (decl)
     tree decl;
{
  return push_template_decl_real (decl, 0);
}

/* Called when a class template TYPE is redeclared with the indicated
   template PARMS, e.g.:

     template <class T> struct S;
     template <class T> struct S {};  */

void 
redeclare_class_template (type, parms)
     tree type;
     tree parms;
{
  tree tmpl = CLASSTYPE_TI_TEMPLATE (type);
  tree tmpl_parms;
  int i;

  if (!PRIMARY_TEMPLATE_P (tmpl))
    /* The type is nested in some template class.  Nothing to worry
       about here; there are no new template parameters for the nested
       type.  */
    return;

  parms = INNERMOST_TEMPLATE_PARMS (parms);
  tmpl_parms = DECL_INNERMOST_TEMPLATE_PARMS (tmpl);

  if (TREE_VEC_LENGTH (parms) != TREE_VEC_LENGTH (tmpl_parms))
    {
      cp_error_at ("previous declaration `%D'", tmpl);
      cp_error ("used %d template parameter%s instead of %d",
		TREE_VEC_LENGTH (tmpl_parms), 
		TREE_VEC_LENGTH (tmpl_parms) == 1 ? "" : "s",
		TREE_VEC_LENGTH (parms));
      return;
    }

  for (i = 0; i < TREE_VEC_LENGTH (tmpl_parms); ++i)
    {
      tree tmpl_parm = TREE_VALUE (TREE_VEC_ELT (tmpl_parms, i));
      tree parm = TREE_VALUE (TREE_VEC_ELT (parms, i));
      tree tmpl_default = TREE_PURPOSE (TREE_VEC_ELT (tmpl_parms, i));
      tree parm_default = TREE_PURPOSE (TREE_VEC_ELT (parms, i));

      if (TREE_CODE (tmpl_parm) != TREE_CODE (parm))
	{
	  cp_error_at ("template parameter `%#D'", tmpl_parm);
	  cp_error ("redeclared here as `%#D'", parm);
	  return;
	}

      if (tmpl_default != NULL_TREE && parm_default != NULL_TREE)
	{
	  /* We have in [temp.param]:

	     A template-parameter may not be given default arguments
	     by two different declarations in the same scope.  */
	  cp_error ("redefinition of default argument for `%#D'", parm);
	  cp_error_at ("  original definition appeared here", tmpl_parm);
	  return;
	}

      if (parm_default != NULL_TREE)
	/* Update the previous template parameters (which are the ones
	   that will really count) with the new default value.  */
	TREE_PURPOSE (TREE_VEC_ELT (tmpl_parms, i)) = parm_default;
    }
}

/* Attempt to convert the non-type template parameter EXPR to the
   indicated TYPE.  If the conversion is successful, return the
   converted value.  If the conversion is unsuccesful, return
   NULL_TREE if we issued an error message, or error_mark_node if we
   did not.  We issue error messages for out-and-out bad template
   parameters, but not simply because the conversion failed, since we
   might be just trying to do argument deduction.  By the time this
   function is called, neither TYPE nor EXPR may make use of template
   parameters.  */

static tree
convert_nontype_argument (type, expr)
     tree type;
     tree expr;
{
  tree expr_type = TREE_TYPE (expr);

  /* A template-argument for a non-type, non-template
     template-parameter shall be one of:

     --an integral constant-expression of integral or enumeration
     type; or
     
     --the name of a non-type template-parameter; or
     
     --the name of an object or function with external linkage,
     including function templates and function template-ids but
     excluding non-static class members, expressed as id-expression;
     or
     
     --the address of an object or function with external linkage,
     including function templates and function template-ids but
     excluding non-static class members, expressed as & id-expression
     where the & is optional if the name refers to a function or
     array; or
     
     --a pointer to member expressed as described in _expr.unary.op_.  */

  /* An integral constant-expression can include const variables
     or enumerators.  */
  if (INTEGRAL_TYPE_P (expr_type) && TREE_READONLY_DECL_P (expr))
    expr = decl_constant_value (expr);

  if (is_overloaded_fn (expr))
    /* OK for now.  We'll check that it has external linkage later.
       Check this first since if expr_type is the unknown_type_node
       we would otherwise complain below.  */
    ;
  else if (INTEGRAL_TYPE_P (expr_type) 
	   || TYPE_PTRMEM_P (expr_type) 
	   || TYPE_PTRMEMFUNC_P (expr_type)
	   /* The next two are g++ extensions.  */
	   || TREE_CODE (expr_type) == REAL_TYPE
	   || TREE_CODE (expr_type) == COMPLEX_TYPE)
    {
      if (! TREE_CONSTANT (expr))
	{
	non_constant:
	  cp_error ("non-constant `%E' cannot be used as template argument",
		    expr);
	  return NULL_TREE;
	}
    }
  else if (TYPE_PTR_P (expr_type) 
	   /* If expr is the address of an overloaded function, we
	      will get the unknown_type_node at this point.  */
	   || expr_type == unknown_type_node)
    {
      tree referent;
      tree e = expr;
      STRIP_NOPS (e);

      if (TREE_CODE (e) != ADDR_EXPR)
	{
	bad_argument:
	  cp_error ("`%E' is not a valid template argument", expr);
	  error ("it must be %s%s with external linkage",
		 TREE_CODE (TREE_TYPE (expr)) == POINTER_TYPE
		 ? "a pointer to " : "",
		 TREE_CODE (TREE_TYPE (TREE_TYPE (expr))) == FUNCTION_TYPE
		 ? "a function" : "an object");
	  return NULL_TREE;
	}

      referent = TREE_OPERAND (e, 0);
      STRIP_NOPS (referent);
      
      if (TREE_CODE (referent) == STRING_CST)
	{
	  cp_error ("string literal %E is not a valid template argument", 
		    referent);
	  error ("because it is the address of an object with static linkage");
	  return NULL_TREE;
	}

      if (is_overloaded_fn (referent))
	/* We'll check that it has external linkage later.  */
	;
      else if (TREE_CODE (referent) != VAR_DECL)
	goto bad_argument;
      else if (!TREE_PUBLIC (referent))
	{
	  cp_error ("address of non-extern `%E' cannot be used as template argument", referent); 
	  return error_mark_node;
	}
    }
  else if (TREE_CODE (expr) == VAR_DECL)
    {
      if (!TREE_PUBLIC (expr))
	goto bad_argument;
    }
  else 
    {
      cp_error ("object `%E' cannot be used as template argument", expr);
      return NULL_TREE;
    }

  switch (TREE_CODE (type))
    {
    case INTEGER_TYPE:
    case BOOLEAN_TYPE:
    case ENUMERAL_TYPE:
      /* For a non-type template-parameter of integral or enumeration
         type, integral promotions (_conv.prom_) and integral
         conversions (_conv.integral_) are applied. */
      if (!INTEGRAL_TYPE_P (expr_type))
	return error_mark_node;
      
      /* It's safe to call digest_init in this case; we know we're
	 just converting one integral constant expression to another.  */
      expr = digest_init (type, expr, (tree*) 0);

      if (TREE_CODE (expr) != INTEGER_CST)
	/* Curiously, some TREE_CONSTNAT integral expressions do not
	   simplify to integer constants.  For example, `3 % 0',
	   remains a TRUNC_MOD_EXPR.  */
	goto non_constant;
      
      return expr;
	
    case REAL_TYPE:
    case COMPLEX_TYPE:
      /* These are g++ extensions.  */
      if (TREE_CODE (expr_type) != TREE_CODE (type))
	return error_mark_node;

      expr = digest_init (type, expr, (tree*) 0);
      
      if (TREE_CODE (expr) != REAL_CST)
	goto non_constant;

      return expr;

    case POINTER_TYPE:
      {
	tree type_pointed_to = TREE_TYPE (type);
 
	if (TYPE_PTRMEM_P (type))
	  /* For a non-type template-parameter of type pointer to data
	     member, qualification conversions (_conv.qual_) are
	     applied.  */
	  return perform_qualification_conversions (type, expr);
	else if (TREE_CODE (type_pointed_to) == FUNCTION_TYPE)
	  { 
	    /* For a non-type template-parameter of type pointer to
	       function, only the function-to-pointer conversion
	       (_conv.func_) is applied.  If the template-argument
	       represents a set of overloaded functions (or a pointer to
	       such), the matching function is selected from the set
	       (_over.over_).  */
	    tree fns;
	    tree fn;

	    if (TREE_CODE (expr) == ADDR_EXPR)
	      fns = TREE_OPERAND (expr, 0);
	    else
	      fns = expr;

	    fn = instantiate_type (type_pointed_to, fns, 0);

	    if (fn == error_mark_node)
	      return error_mark_node;

	    if (!TREE_PUBLIC (fn))
	      {
		if (really_overloaded_fn (fns))
		  return error_mark_node;
		else
		  goto bad_argument;
	      }

	    expr = build_unary_op (ADDR_EXPR, fn, 0);

	    my_friendly_assert (comptypes (type, TREE_TYPE (expr), 1), 
				0);
	    return expr;
	  }
	else 
	  {
	    /* For a non-type template-parameter of type pointer to
	       object, qualification conversions (_conv.qual_) and the
	       array-to-pointer conversion (_conv.array_) are applied.
	       [Note: In particular, neither the null pointer conversion
	       (_conv.ptr_) nor the derived-to-base conversion
	       (_conv.ptr_) are applied.  Although 0 is a valid
	       template-argument for a non-type template-parameter of
	       integral type, it is not a valid template-argument for a
	       non-type template-parameter of pointer type.]  
	    
	       The call to decay_conversion performs the
	       array-to-pointer conversion, if appropriate.  */
	    expr = decay_conversion (expr);

	    if (expr == error_mark_node)
	      return error_mark_node;
	    else
	      return perform_qualification_conversions (type, expr);
	  }
      }
      break;

    case REFERENCE_TYPE:
      {
	tree type_referred_to = TREE_TYPE (type);

	if (TREE_CODE (type_referred_to) == FUNCTION_TYPE)
	  {
	    /* For a non-type template-parameter of type reference to
	       function, no conversions apply.  If the
	       template-argument represents a set of overloaded
	       functions, the matching function is selected from the
	       set (_over.over_).  */
	    tree fns = expr;
	    tree fn;

	    fn = instantiate_type (type_referred_to, fns, 0);

	    if (!TREE_PUBLIC (fn))
	      {
		if (really_overloaded_fn (fns))
		  /* Don't issue an error here; we might get a different
		     function if the overloading had worked out
		     differently.  */
		  return error_mark_node;
		else
		  goto bad_argument;
	      }

	    if (fn == error_mark_node)
	      return error_mark_node;

	    my_friendly_assert (comptypes (type, TREE_TYPE (fn), 1),
				0);

	    return fn;
	  }
	else
	  {
	    /* For a non-type template-parameter of type reference to
	       object, no conversions apply.  The type referred to by the
	       reference may be more cv-qualified than the (otherwise
	       identical) type of the template-argument.  The
	       template-parameter is bound directly to the
	       template-argument, which must be an lvalue.  */
	    if (!comptypes (TYPE_MAIN_VARIANT (expr_type),
			    TYPE_MAIN_VARIANT (type), 1)
		|| (TYPE_READONLY (expr_type) >
		    TYPE_READONLY (type_referred_to))
		|| (TYPE_VOLATILE (expr_type) >
		    TYPE_VOLATILE (type_referred_to))
		|| !real_lvalue_p (expr))
	      return error_mark_node;
	    else
	      return expr;
	  }
      }
      break;

    case RECORD_TYPE:
      {
	tree fns;
	tree fn;

	if (!TYPE_PTRMEMFUNC_P (type))
	  /* This handles templates like
	       template<class T, T t> void f();
	     when T is substituted with any class.  The second template
	     parameter becomes invalid and the template candidate is
	     rejected.  */
	  return error_mark_node;

	/* For a non-type template-parameter of type pointer to member
	   function, no conversions apply.  If the template-argument
	   represents a set of overloaded member functions, the
	   matching member function is selected from the set
	   (_over.over_).  */

	if (!TYPE_PTRMEMFUNC_P (expr_type) && 
	    expr_type != unknown_type_node)
	  return error_mark_node;

	if (TREE_CODE (expr) == CONSTRUCTOR)
	  {
	    /* A ptr-to-member constant.  */
	    if (!comptypes (type, expr_type, 1))
	      return error_mark_node;
	    else 
	      return expr;
	  }

	if (TREE_CODE (expr) != ADDR_EXPR)
	  return error_mark_node;

	fns = TREE_OPERAND (expr, 0);
	
	fn = instantiate_type (TREE_TYPE (TREE_TYPE (type)), 
			       fns, 0);
	
	if (fn == error_mark_node)
	  return error_mark_node;

	expr = build_unary_op (ADDR_EXPR, fn, 0);
	
	my_friendly_assert (comptypes (type, TREE_TYPE (expr), 1), 
			    0);
	return expr;
      }
      break;

    default:
      /* All non-type parameters must have one of these types.  */
      my_friendly_abort (0);
      break;
    }

  return error_mark_node;
}

/* Return 1 if PARM_PARMS and ARG_PARMS matches using rule for 
   template template parameters.  Both PARM_PARMS and ARG_PARMS are 
   vectors of TREE_LIST nodes containing TYPE_DECL, TEMPLATE_DECL 
   or PARM_DECL.
   
   ARG_PARMS may contain more parameters than PARM_PARMS.  If this is 
   the case, then extra parameters must have default arguments.

   Consider the example:
     template <class T, class Allocator = allocator> class vector;
     template<template <class U> class TT> class C;

   C<vector> is a valid instantiation.  PARM_PARMS for the above code 
   contains a TYPE_DECL (for U),  ARG_PARMS contains two TYPE_DECLs (for 
   T and Allocator) and OUTER_ARGS contains the argument that is used to 
   substitute the TT parameter.  */

static int
coerce_template_template_parms (parm_parms, arg_parms, in_decl, outer_args)
     tree parm_parms, arg_parms, in_decl, outer_args;
{
  int nparms, nargs, i;
  tree parm, arg;

  my_friendly_assert (TREE_CODE (parm_parms) == TREE_VEC, 0);
  my_friendly_assert (TREE_CODE (arg_parms) == TREE_VEC, 0);

  nparms = TREE_VEC_LENGTH (parm_parms);
  nargs = TREE_VEC_LENGTH (arg_parms);

  /* The rule here is opposite of coerce_template_parms.  */
  if (nargs < nparms
      || (nargs > nparms
	  && TREE_PURPOSE (TREE_VEC_ELT (arg_parms, nparms)) == NULL_TREE))
    return 0;

  for (i = 0; i < nparms; ++i)
    {
      parm = TREE_VALUE (TREE_VEC_ELT (parm_parms, i));
      arg = TREE_VALUE (TREE_VEC_ELT (arg_parms, i));

      if (arg == NULL_TREE || arg == error_mark_node
          || parm == NULL_TREE || parm == error_mark_node)
	return 0;

      if (TREE_CODE (arg) != TREE_CODE (parm))
        return 0;

      switch (TREE_CODE (parm))
	{
	case TYPE_DECL:
	  break;

	case TEMPLATE_DECL:
	  /* We encounter instantiations of templates like
	       template <template <template <class> class> class TT>
	       class C;  */
	  sorry ("nested template template parameter");
	  return 0;

	case PARM_DECL:
	  /* The tsubst call is used to handle cases such as
	       template <class T, template <T> class TT> class D;  
	     i.e. the parameter list of TT depends on earlier parameters.  */
	  if (!comptypes (tsubst (TREE_TYPE (parm), outer_args, in_decl), 
			  TREE_TYPE (arg), 1))
	    return 0;
	  break;
	  
	default:
	  my_friendly_abort (0);
	}
    }
  return 1;
}

/* Convert all template arguments to their appropriate types, and return
   a vector containing the resulting values.  If any error occurs, return
   error_mark_node, and, if COMPLAIN is non-zero, issue an error message.
   Some error messages are issued even if COMPLAIN is zero; for
   instance, if a template argument is composed from a local class. 

   If REQUIRE_ALL_ARGUMENTS is non-zero, all arguments must be
   provided in ARGLIST, or else trailing parameters must have default
   values.  If REQUIRE_ALL_ARGUMENTS is zero, we will attempt argument
   deduction for any unspecified trailing arguments.  */
   
static tree
coerce_template_parms (parms, arglist, in_decl,
		       complain,
		       require_all_arguments)
     tree parms, arglist;
     tree in_decl;
     int complain;
     int require_all_arguments;
{
  int nparms, nargs, i, lost = 0;
  tree inner_args;
  tree vec;

  inner_args = innermost_args (arglist);
  nargs = NUM_TMPL_ARGS (inner_args);
  nparms = TREE_VEC_LENGTH (parms);

  if (nargs > nparms
      || (nargs < nparms
	  && require_all_arguments
	  && TREE_PURPOSE (TREE_VEC_ELT (parms, nargs)) == NULL_TREE))
    {
      if (complain) 
	{
	  cp_error ("wrong number of template arguments (%d, should be %d)",
		    nargs, nparms);
	  
	  if (in_decl)
	    cp_error_at ("provided for `%D'", in_decl);
	}

      return error_mark_node;
    }

  /* Create in VEC the appropriate innermost arguments, and reset
     ARGLIST to contain the complete set of arguments.  */
  if (inner_args && TREE_CODE (inner_args) == TREE_VEC && nargs == nparms)
    {
      /* If we already have all the arguments, we can just use them.
	 This is an optimization over the code in the `else' branch
	 below, and should be functionally identicial.  */
      vec = copy_node (inner_args);
      arglist = add_outermost_template_args (arglist, vec);
    }
  else
    {
      /* If we don't already have all the arguments we must get what
	 we can from default template arguments.  The tricky bit is
	 that previous arguments can influence the default values,
	 e.g.:  

	   template <class T, class U = T> void foo();

	 If we see `foo<int>' we have to come up with an {int, int}
	 vector.  */

      tree new_arglist;

      vec = make_tree_vec (nparms);
      new_arglist = add_outermost_template_args (arglist, vec);

      for (i = 0; i < nparms; i++)
	{
	  tree arg;
	  tree parm = TREE_VEC_ELT (parms, i);

	  if (arglist && TREE_CODE (arglist) == TREE_LIST)
	    {
	      arg = arglist;
	      arglist = TREE_CHAIN (arglist);

	      if (arg == error_mark_node)
		lost++;
	      else
		arg = TREE_VALUE (arg);
	    }
	  else if (i < nargs)
	    {
	      arg = TREE_VEC_ELT (inner_args, i);
	      if (arg == error_mark_node)
		lost++;
	    }
	  /* If no template argument was supplied, look for a default
	     value.  */
	  else if (TREE_PURPOSE (parm) == NULL_TREE)
	    {
	      /* There was no default value.  */
	      my_friendly_assert (!require_all_arguments, 0);
	      break;
	    }
	  else if (TREE_CODE (TREE_VALUE (parm)) == TYPE_DECL)
	    arg = tsubst (TREE_PURPOSE (parm), new_arglist, in_decl);
	  else
	    arg = tsubst_expr (TREE_PURPOSE (parm), new_arglist, in_decl);

	  TREE_VEC_ELT (vec, i) = arg;
	}

      /* We've left ARGLIST intact up to this point, in order to allow
	 iteration through it in the case that it was a TREE_LIST, but
	 from here on it should contain the full set of template
	 arguments.  */
      arglist = new_arglist;
    }

  for (i = 0; i < nparms; i++)
    {
      tree arg = TREE_VEC_ELT (vec, i);
      tree parm = TREE_VALUE (TREE_VEC_ELT (parms, i));
      tree val = 0;
      int is_type, requires_type, is_tmpl_type, requires_tmpl_type;

      if (arg == NULL_TREE)
	/* We're out of arguments.  */
	{
	  my_friendly_assert (!require_all_arguments, 0);
	  break;
	}

      if (arg == error_mark_node)
	{
	  cp_error ("template argument %d is invalid", i + 1);
	  lost++;
	  continue;
	}

      if (TREE_CODE (arg) == TREE_LIST 
	  && TREE_TYPE (arg) != NULL_TREE
	  && TREE_CODE (TREE_TYPE (arg)) == OFFSET_TYPE)
	{  
	  /* The template argument was the name of some
	     member function.  That's usually
	     illegal, but static members are OK.  In any
	     case, grab the underlying fields/functions
	     and issue an error later if required.  */
	  arg = TREE_VALUE (arg);
	  TREE_TYPE (arg) = unknown_type_node;
	}

      requires_tmpl_type = TREE_CODE (parm) == TEMPLATE_DECL;
      requires_type = TREE_CODE (parm) == TYPE_DECL
		      || requires_tmpl_type;

      /* Check if it is a class template.  If REQUIRES_TMPL_TYPE is true,
	 we also accept implicitly created TYPE_DECL as a valid argument.
         This is necessary to handle the case where we pass a template name
         to a template template parameter in a scope where we've derived from
         in instantiation of that template, so the template name refers to that
         instantiation.  We really ought to handle this better.  */
      is_tmpl_type = (TREE_CODE (arg) == TEMPLATE_DECL
		      && TREE_CODE (DECL_TEMPLATE_RESULT (arg)) == TYPE_DECL)
		     || (TREE_CODE (arg) == TEMPLATE_TEMPLATE_PARM
			 && !CLASSTYPE_TEMPLATE_INFO (arg))
		     || (TREE_CODE (arg) == RECORD_TYPE
		         && CLASSTYPE_TEMPLATE_INFO (arg)
		         && TREE_CODE (TYPE_NAME (arg)) == TYPE_DECL
			 && DECL_ARTIFICIAL (TYPE_NAME (arg))
			 && requires_tmpl_type
			 && current_class_type
			 /* FIXME what about nested types?  */
			 && get_binfo (arg, current_class_type, 0));
      if (is_tmpl_type && TREE_CODE (arg) == TEMPLATE_TEMPLATE_PARM)
	arg = TYPE_STUB_DECL (arg);
      else if (is_tmpl_type && TREE_CODE (arg) == RECORD_TYPE)
	arg = CLASSTYPE_TI_TEMPLATE (arg);

      is_type = TREE_CODE_CLASS (TREE_CODE (arg)) == 't' || is_tmpl_type;

      if (requires_type && ! is_type && TREE_CODE (arg) == SCOPE_REF
	  && TREE_CODE (TREE_OPERAND (arg, 0)) == TEMPLATE_TYPE_PARM)
	{
	  cp_pedwarn ("to refer to a type member of a template parameter,");
	  cp_pedwarn ("  use `typename %E'", arg);

	  arg = make_typename_type (TREE_OPERAND (arg, 0),
				    TREE_OPERAND (arg, 1));
	  is_type = 1;
	}
      if (is_type != requires_type)
	{
	  if (in_decl)
	    {
	      if (complain)
		{
		  cp_error ("type/value mismatch at argument %d in template parameter list for `%D'",
			    i + 1, in_decl);
		  if (is_type)
		    cp_error ("  expected a constant of type `%T', got `%T'",
			      TREE_TYPE (parm),
			      (is_tmpl_type ? DECL_NAME (arg) : arg));
		  else
		    cp_error ("  expected a type, got `%E'", arg);
		}
	    }
	  lost++;
	  TREE_VEC_ELT (vec, i) = error_mark_node;
	  continue;
	}
      if (is_tmpl_type ^ requires_tmpl_type)
	{
	  if (in_decl && complain)
	    {
	      cp_error ("type/value mismatch at argument %d in template parameter list for `%D'",
			i + 1, in_decl);
	      if (is_tmpl_type)
		cp_error ("  expected a type, got `%T'", DECL_NAME (arg));
	      else
		cp_error ("  expected a class template, got `%T'", arg);
	    }
	  lost++;
	  TREE_VEC_ELT (vec, i) = error_mark_node;
	  continue;
	}
        
      if (is_type)
	{
	  if (requires_tmpl_type)
	    {
	      tree parmparm = DECL_INNERMOST_TEMPLATE_PARMS (parm);
	      tree argparm = DECL_INNERMOST_TEMPLATE_PARMS (arg);

	      if (coerce_template_template_parms (parmparm, argparm, 
						  in_decl, vec))
		{
		  val = arg;

		  /* TEMPLATE_TEMPLATE_PARM node is preferred over 
		     TEMPLATE_DECL.  */
		  if (val != error_mark_node 
		      && DECL_TEMPLATE_TEMPLATE_PARM_P (val))
		    val = TREE_TYPE (val);
		}
	      else
		{
		  if (in_decl && complain)
		    {
		      cp_error ("type/value mismatch at argument %d in template parameter list for `%D'",
				i + 1, in_decl);
		      cp_error ("  expected a template of type `%D', got `%D'", parm, arg);
		    }

		  val = error_mark_node;
		}
	    }
	  else
	    {
	      val = groktypename (arg);
	      if (! processing_template_decl)
		{
		  tree t = target_type (val);
		  if (((IS_AGGR_TYPE (t) && TREE_CODE (t) != TYPENAME_TYPE)
		       || TREE_CODE (t) == ENUMERAL_TYPE)
		      && decl_function_context (TYPE_MAIN_DECL (t)))
		    {
		      cp_error ("type `%T' composed from a local type is not a valid template-argument",
				val);
		      return error_mark_node;
		    }
		}
	    }
	}
      else
	{
	  tree t = tsubst (TREE_TYPE (parm), arglist, in_decl);

	  if (processing_template_decl)
	    arg = maybe_fold_nontype_arg (arg);

	  if (!uses_template_parms (arg) && !uses_template_parms (t))
	    /* We used to call digest_init here.  However, digest_init
	       will report errors, which we don't want when complain
	       is zero.  More importantly, digest_init will try too
	       hard to convert things: for example, `0' should not be
	       converted to pointer type at this point according to
	       the standard.  Accepting this is not merely an
	       extension, since deciding whether or not these
	       conversions can occur is part of determining which
	       function template to call, or whether a given epxlicit
	       argument specification is legal.  */
	    val = convert_nontype_argument (t, arg);
	  else
	    val = arg;

	  if (val == NULL_TREE)
	    val = error_mark_node;
	  else if (val == error_mark_node && complain)
	    cp_error ("could not convert template argument `%E' to `%T'", 
		      arg, t);
	}

      if (val == error_mark_node)
	lost++;

      TREE_VEC_ELT (vec, i) = val;
    }
  if (lost)
    return error_mark_node;
  return vec;
}

/* Renturns 1 iff the OLDARGS and NEWARGS are in fact identical sets
   of template arguments.  Returns 0 otherwise.  */

int
comp_template_args (oldargs, newargs)
     tree oldargs, newargs;
{
  int i;

  if (TREE_VEC_LENGTH (oldargs) != TREE_VEC_LENGTH (newargs))
    return 0;

  for (i = 0; i < TREE_VEC_LENGTH (oldargs); ++i)
    {
      tree nt = TREE_VEC_ELT (newargs, i);
      tree ot = TREE_VEC_ELT (oldargs, i);

      if (nt == ot)
	continue;
      if (TREE_CODE (nt) != TREE_CODE (ot))
	return 0;
      if (TREE_CODE (nt) == TREE_VEC)
        {
          /* For member templates */
	  if (comp_template_args (ot, nt))
	    continue;
        }
      else if (TREE_CODE_CLASS (TREE_CODE (ot)) == 't')
	{
	  if (comptypes (ot, nt, 1))
	    continue;
	}
      else if (cp_tree_equal (ot, nt) > 0)
	continue;
      return 0;
    }
  return 1;
}

/* Given class template name and parameter list, produce a user-friendly name
   for the instantiation.  */

static char *
mangle_class_name_for_template (name, parms, arglist)
     char *name;
     tree parms, arglist;
{
  static struct obstack scratch_obstack;
  static char *scratch_firstobj;
  int i, nparms;

  if (!scratch_firstobj)
    gcc_obstack_init (&scratch_obstack);
  else
    obstack_free (&scratch_obstack, scratch_firstobj);
  scratch_firstobj = obstack_alloc (&scratch_obstack, 1);

#define ccat(c)	obstack_1grow (&scratch_obstack, (c));
#define cat(s)	obstack_grow (&scratch_obstack, (s), strlen (s))

  cat (name);
  ccat ('<');
  nparms = TREE_VEC_LENGTH (parms);
  arglist = innermost_args (arglist);
  my_friendly_assert (nparms == TREE_VEC_LENGTH (arglist), 268);
  for (i = 0; i < nparms; i++)
    {
      tree parm = TREE_VALUE (TREE_VEC_ELT (parms, i));
      tree arg = TREE_VEC_ELT (arglist, i);

      if (i)
	ccat (',');

      if (TREE_CODE (parm) == TYPE_DECL)
	{
	  cat (type_as_string_real (arg, 0, 1));
	  continue;
	}
      else if (TREE_CODE (parm) == TEMPLATE_DECL)
	{
	  if (TREE_CODE (arg) == TEMPLATE_DECL)
	    {
	      /* Already substituted with real template.  Just output 
		 the template name here */
              tree context = DECL_CONTEXT (arg);
	      if (context)
		{
                  my_friendly_assert (TREE_CODE (context) == NAMESPACE_DECL, 980422);
		  cat(decl_as_string (DECL_CONTEXT (arg), 0));
		  cat("::");
		}
	      cat (IDENTIFIER_POINTER (DECL_NAME (arg)));
	    }
	  else
	    /* Output the parameter declaration */
	    cat (type_as_string_real (arg, 0, 1));
	  continue;
	}
      else
	my_friendly_assert (TREE_CODE (parm) == PARM_DECL, 269);

      if (TREE_CODE (arg) == TREE_LIST)
	{
	  /* New list cell was built because old chain link was in
	     use.  */
	  my_friendly_assert (TREE_PURPOSE (arg) == NULL_TREE, 270);
	  arg = TREE_VALUE (arg);
	}
      /* No need to check arglist against parmlist here; we did that
	 in coerce_template_parms, called from lookup_template_class.  */
      cat (expr_as_string (arg, 0));
    }
  {
    char *bufp = obstack_next_free (&scratch_obstack);
    int offset = 0;
    while (bufp[offset - 1] == ' ')
      offset--;
    obstack_blank_fast (&scratch_obstack, offset);

    /* B<C<char> >, not B<C<char>> */
    if (bufp[offset - 1] == '>')
      ccat (' ');
  }
  ccat ('>');
  ccat ('\0');
  return (char *) obstack_base (&scratch_obstack);
}

static tree
classtype_mangled_name (t)
     tree t;
{
  if (CLASSTYPE_TEMPLATE_INFO (t)
      /* Specializations have already had their names set up in
	 lookup_template_class.  */
      && !CLASSTYPE_TEMPLATE_SPECIALIZATION (t)
      /* For non-primary templates, the template parameters are
	 implicit from their surrounding context.  */
      && PRIMARY_TEMPLATE_P (CLASSTYPE_TI_TEMPLATE (t)))
    {
      tree name = DECL_NAME (CLASSTYPE_TI_TEMPLATE (t));
      char *mangled_name = mangle_class_name_for_template
	(IDENTIFIER_POINTER (name),
	 DECL_INNERMOST_TEMPLATE_PARMS (CLASSTYPE_TI_TEMPLATE (t)),
	 CLASSTYPE_TI_ARGS (t));
      tree id = get_identifier (mangled_name);
      IDENTIFIER_TEMPLATE (id) = name;
      return id;
    }
  else
    return TYPE_IDENTIFIER (t);
}

static void
add_pending_template (d)
     tree d;
{
  tree ti;

  if (TREE_CODE_CLASS (TREE_CODE (d)) == 't')
    ti = CLASSTYPE_TEMPLATE_INFO (d);
  else
    ti = DECL_TEMPLATE_INFO (d);

  if (TI_PENDING_TEMPLATE_FLAG (ti))
    return;

  *template_tail = perm_tree_cons
    (build_srcloc_here (), d, NULL_TREE);
  template_tail = &TREE_CHAIN (*template_tail);
  TI_PENDING_TEMPLATE_FLAG (ti) = 1;
}


/* Return a TEMPLATE_ID_EXPR corresponding to the indicated FNS (which
   may be either a _DECL or an overloaded function or an
   IDENTIFIER_NODE), and ARGLIST.  */

tree
lookup_template_function (fns, arglist)
     tree fns, arglist;
{
  tree type;

  if (fns == NULL_TREE)
    {
      cp_error ("non-template used as template");
      return error_mark_node;
    }

  type = TREE_TYPE (fns);
  if (TREE_CODE (fns) == OVERLOAD || !type)
    type = unknown_type_node;

  if (processing_template_decl)
    return build_min (TEMPLATE_ID_EXPR, type, fns, arglist);  
  else
    return build (TEMPLATE_ID_EXPR, type, fns, arglist);
}

/* Within the scope of a template class S<T>, the name S gets bound
   (in build_self_reference) to a TYPE_DECL for the class, not a
   TEMPLATE_DECL.  If DECL is a TYPE_DECL for current_class_type,
   or one of its enclosing classes, and that type is a template,
   return the associated TEMPLATE_DECL.  Otherwise, the original
   DECL is returned.  */

tree
maybe_get_template_decl_from_type_decl (decl)
     tree decl;
{
  return (decl != NULL_TREE
	  && TREE_CODE (decl) == TYPE_DECL 
	  && DECL_ARTIFICIAL (decl)
	  && CLASSTYPE_TEMPLATE_INFO (TREE_TYPE (decl))) 
    ? CLASSTYPE_TI_TEMPLATE (TREE_TYPE (decl)) : decl;
}

/* Given an IDENTIFIER_NODE (type TEMPLATE_DECL) and a chain of
   parameters, find the desired type.

   D1 is the PTYPENAME terminal, and ARGLIST is the list of arguments.
   (Actually ARGLIST may be either a TREE_LIST or a TREE_VEC.  It will
   be a TREE_LIST if called directly from the parser, and a TREE_VEC
   otherwise.)  Since ARGLIST is build on the decl_obstack, we must
   copy it here to keep it from being reclaimed when the decl storage
   is reclaimed.

   IN_DECL, if non-NULL, is the template declaration we are trying to
   instantiate.  

   If ENTERING_SCOPE is non-zero, we are about to enter the scope of
   the class we are looking up.

   If the template class is really a local class in a template
   function, then the FUNCTION_CONTEXT is the function in which it is
   being instantiated.  */

tree
lookup_template_class (d1, arglist, in_decl, context, entering_scope)
     tree d1, arglist;
     tree in_decl;
     tree context;
     int entering_scope;
{
  tree template = NULL_TREE, parmlist;
  char *mangled_name;
  tree id, t;

  if (TREE_CODE (d1) == IDENTIFIER_NODE)
    {
      if (IDENTIFIER_LOCAL_VALUE (d1) 
	  && DECL_TEMPLATE_TEMPLATE_PARM_P (IDENTIFIER_LOCAL_VALUE (d1)))
	template = IDENTIFIER_LOCAL_VALUE (d1);
      else
	{
	  if (context)
	    push_decl_namespace (context);
	  if (current_class_type != NULL_TREE)
	    template = 
	      maybe_get_template_decl_from_type_decl
	      (IDENTIFIER_CLASS_VALUE (d1));
	  if (template == NULL_TREE)
	    template = lookup_name_nonclass (d1);
	  if (context)
	    pop_decl_namespace ();
	}
      if (template)
	context = DECL_CONTEXT (template);
    }
  else if (TREE_CODE (d1) == TYPE_DECL && IS_AGGR_TYPE (TREE_TYPE (d1)))
    {
      if (CLASSTYPE_TEMPLATE_INFO (TREE_TYPE (d1)) == NULL_TREE)
	return error_mark_node;
      template = CLASSTYPE_TI_TEMPLATE (TREE_TYPE (d1));
      d1 = DECL_NAME (template);
    }
  else if (TREE_CODE (d1) == ENUMERAL_TYPE 
	   || (TREE_CODE_CLASS (TREE_CODE (d1)) == 't' 
	       && IS_AGGR_TYPE (d1)))
    {
      template = TYPE_TI_TEMPLATE (d1);
      d1 = DECL_NAME (template);
    }
  else if (TREE_CODE (d1) == TEMPLATE_DECL
	   && TREE_CODE (DECL_RESULT (d1)) == TYPE_DECL)
    {
      template = d1;
      d1 = DECL_NAME (template);
      context = DECL_CONTEXT (template);
    }
  else
    my_friendly_abort (272);

  /* With something like `template <class T> class X class X { ... };'
     we could end up with D1 having nothing but an IDENTIFIER_LOCAL_VALUE.
     We don't want to do that, but we have to deal with the situation, so
     let's give them some syntax errors to chew on instead of a crash.  */
  if (! template)
    return error_mark_node;

  if (context == NULL_TREE)
    context = global_namespace;

  if (TREE_CODE (template) != TEMPLATE_DECL)
    {
      cp_error ("non-template type `%T' used as a template", d1);
      if (in_decl)
	cp_error_at ("for template declaration `%D'", in_decl);
      return error_mark_node;
    }

  if (DECL_TEMPLATE_TEMPLATE_PARM_P (template))
    {
      /* Create a new TEMPLATE_DECL and TEMPLATE_TEMPLATE_PARM node to store
         template arguments */

      tree parm = copy_template_template_parm (TREE_TYPE (template));
      tree template2 = TYPE_STUB_DECL (parm);
      tree arglist2;

      CLASSTYPE_GOT_SEMICOLON (parm) = 1;
      parmlist = DECL_INNERMOST_TEMPLATE_PARMS (template);

      arglist2 = coerce_template_parms (parmlist, arglist, template, 1, 1);
      if (arglist2 == error_mark_node)
	return error_mark_node;

      arglist2 = copy_to_permanent (arglist2);
      CLASSTYPE_TEMPLATE_INFO (parm)
	= perm_tree_cons (template2, arglist2, NULL_TREE);
      TYPE_SIZE (parm) = 0;
      return parm;
    }
  else 
    {
      tree template_type = TREE_TYPE (template);
      tree type_decl;
      tree found = NULL_TREE;
      int arg_depth;
      int parm_depth;

      template = most_general_template (template);
      parmlist = DECL_TEMPLATE_PARMS (template);
      parm_depth = TMPL_PARMS_DEPTH (parmlist);
      arg_depth = TMPL_ARGS_DEPTH (arglist);

      if (arg_depth == 1 && parm_depth > 1)
	{
	  /* We've been given an incomplete set of template arguments.
	     For example, given:

	       template <class T> struct S1 {
	         template <class U> struct S2 {};
		 template <class U> struct S2<U*> {};
	        };
	     
	     we will be called with an ARGLIST of `U*', but the
	     TEMPLATE will be `template <class T> template
	     <class U> struct S1<T>::S2'.  We must fill in the missing
	     arguments.  */
	  my_friendly_assert (context != NULL_TREE, 0);
	  while (!IS_AGGR_TYPE_CODE (TREE_CODE (context))
		 && context != global_namespace)
	    context = DECL_REAL_CONTEXT (context);

	  if (context == global_namespace)
	    /* This is bad.  We cannot get enough arguments, even from
	       the surrounding context, to resolve this class.  One
	       case where this might happen is (illegal) code like:

	           template <class U> 
		   template <class T>
		   struct S { 
		     A(const A<T>& a) {}
		   };  
	    
	       We should catch this error sooner (at the opening curly
	       for `S', but it is better to be safe than sorry here.  */
	    {
	      cp_error ("invalid use of `%D'", template);
	      return error_mark_node;
	    }

	  arglist = add_to_template_args (TYPE_TI_ARGS (context),
					  arglist);
	  arg_depth = TMPL_ARGS_DEPTH (arglist);
	}

      my_friendly_assert (parm_depth == arg_depth, 0);
      
      /* Calculate the BOUND_ARGS.  These will be the args that are
	 actually tsubst'd into the definition to create the
	 instantiation.  */
      if (parm_depth > 1)
	{
	  /* We have multiple levels of arguments to coerce, at once.  */
	  int i;
	  int saved_depth = TMPL_ARGS_DEPTH (arglist);

	  tree bound_args = make_tree_vec (parm_depth);
	  
	  for (i = saved_depth,
		 t = DECL_TEMPLATE_PARMS (template); 
	       i > 0 && t != NULL_TREE;
	       --i, t = TREE_CHAIN (t))
	    {
	      tree a = coerce_template_parms (TREE_VALUE (t),
					      arglist, template, 1, 1);
	      SET_TMPL_ARGS_LEVEL (bound_args, i, a);

	      /* We temporarily reduce the length of the ARGLIST so
		 that coerce_template_parms will see only the arguments
		 corresponding to the template parameters it is
		 examining.  */
	      TREE_VEC_LENGTH (arglist)--;
	    }

	  /* Restore the ARGLIST to its full size.  */
	  TREE_VEC_LENGTH (arglist) = saved_depth;

	  arglist = bound_args;
	}
      else
	arglist
	  = coerce_template_parms (INNERMOST_TEMPLATE_PARMS (parmlist),
				   innermost_args (arglist),
				   template, 1, 1);

      if (arglist == error_mark_node)
	/* We were unable to bind the arguments.  */
	return error_mark_node;

      /* In the scope of a template class, explicit references to the
	 template class refer to the type of the template, not any
	 instantiation of it.  For example, in:
	 
	   template <class T> class C { void f(C<T>); }

	 the `C<T>' is just the same as `C'.  Outside of the
	 class, however, such a reference is an instantiation.  */
      if (comp_template_args (TYPE_TI_ARGS (template_type),
			      arglist))
	{
	  found = template_type;
	  
	  if (!entering_scope && PRIMARY_TEMPLATE_P (template))
	    {
	      tree ctx;
	      
	      /* Note that we use DECL_CONTEXT, rather than
		 CP_DECL_CONTEXT, so that the termination test is
		 always just `ctx'.  We're not interested in namepace
		 scopes.  */
	      for (ctx = current_class_type; 
		   ctx; 
		   ctx = (TREE_CODE_CLASS (TREE_CODE (ctx)) == 't') 
		     ? TYPE_CONTEXT (ctx) : DECL_CONTEXT (ctx))
		if (comptypes (ctx, template_type, 1))
		  break;
	      
	      if (!ctx)
		/* We're not in the scope of the class, so the
		   TEMPLATE_TYPE is not the type we want after
		   all.  */
		found = NULL_TREE;
	    }
	}
      
      if (!found)
	{
	  for (found = DECL_TEMPLATE_INSTANTIATIONS (template);
	       found; found = TREE_CHAIN (found))
	    if (comp_template_args (TREE_PURPOSE (found), arglist))
	      break;

	  if (found)
	    found = TREE_VALUE (found);
	}
      
      if (found)
	{
	  if (can_free (&permanent_obstack, arglist))
	    obstack_free (&permanent_obstack, arglist);
	  return found;
	}

      /* Since we didn't find the type, we'll have to create it.
	 Since we'll be saving this type on the
	 DECL_TEMPLATE_INSTANTIATIONS list, it must be permanent.  */
      push_obstacks (&permanent_obstack, &permanent_obstack);
      
      /* Create the type.  */
      if (TREE_CODE (template_type) == ENUMERAL_TYPE)
	{
	  if (!uses_template_parms (arglist))
	    t = tsubst_enum (template_type, arglist);
	  else
	    /* We don't want to call tsubst_enum for this type, since
	       the values for the enumeration constants may involve
	       template parameters.  And, no one should be interested
	       in the enumeration constants for such a type.  */
	    t = make_node (ENUMERAL_TYPE);
	}
      else
	{
	  t = make_lang_type (TREE_CODE (template_type));
	  CLASSTYPE_DECLARED_CLASS (t) 
	    = CLASSTYPE_DECLARED_CLASS (template_type);
	  CLASSTYPE_GOT_SEMICOLON (t) = 1;
	  SET_CLASSTYPE_IMPLICIT_INSTANTIATION (t);
	}

      /* If we called tsubst_enum above, this information will already
	 be set up.  */
      if (!TYPE_NAME (t))
	{
	  TYPE_CONTEXT (t) = FROB_CONTEXT (context);
	  
	  /* Create a stub TYPE_DECL for it.  */
	  type_decl = build_decl (TYPE_DECL, DECL_NAME (template), t);
	  SET_DECL_ARTIFICIAL (type_decl);
	  DECL_CONTEXT (type_decl) = TYPE_CONTEXT (t);
	  DECL_SOURCE_FILE (type_decl) 
	    = DECL_SOURCE_FILE (TYPE_STUB_DECL (template_type));
	  DECL_SOURCE_LINE (type_decl) 
	    = DECL_SOURCE_LINE (TYPE_STUB_DECL (template_type));
	  TYPE_STUB_DECL (t) = TYPE_NAME (t) = type_decl;
	}
      else
	type_decl = TYPE_NAME (t);

      /* We're done with the permanent obstack, now.  */
      pop_obstacks ();

      /* Set up the template information.  */
      arglist = copy_to_permanent (arglist);
      SET_TYPE_TEMPLATE_INFO (t,
			      perm_tree_cons (template, arglist, NULL_TREE));
      DECL_TEMPLATE_INSTANTIATIONS (template) = perm_tree_cons
	(arglist, t, DECL_TEMPLATE_INSTANTIATIONS (template));

      /* Reset the name of the type, now that CLASSTYPE_TEMPLATE_INFO
	 is set up.  */
      if (TREE_CODE (t) != ENUMERAL_TYPE)
	DECL_NAME (type_decl) = classtype_mangled_name (t);
      DECL_ASSEMBLER_NAME (type_decl) = DECL_NAME (type_decl);
      if (! uses_template_parms (arglist))
	{
	  DECL_ASSEMBLER_NAME (type_decl)
	    = get_identifier (build_overload_name (t, 1, 1));
	  
	  if (TREE_CODE (t) != ENUMERAL_TYPE
	      && flag_external_templates
	      && CLASSTYPE_INTERFACE_KNOWN (TREE_TYPE (template))
	      && ! CLASSTYPE_INTERFACE_ONLY (TREE_TYPE (template)))
	    add_pending_template (t);
	}
      else
	/* If the type makes use of template parameters, the
	   code that generates debugging information will crash.  */
	DECL_IGNORED_P (TYPE_STUB_DECL (t)) = 1;

      return t;
    }
}

/* For each TEMPLATE_TYPE_PARM, TEMPLATE_TEMPLATE_PARM, or
   TEMPLATE_PARM_INDEX in T, call FN with the parameter and the DATA.
   If FN returns non-zero, the iteration is terminated, and
   for_each_template_parm returns 1.  Otherwise, the iteration
   continues.  If FN never returns a non-zero value, the value
   returned by for_each_template_parm is 0.  If FN is NULL, it is
   considered to be the function which always returns 1.  */

int
for_each_template_parm (t, fn, data)
     tree t;
     tree_fn_t fn;
     void* data;
{
  if (!t)
    return 0;

  if (TREE_CODE_CLASS (TREE_CODE (t)) == 't'
      && for_each_template_parm (TYPE_CONTEXT (t), fn, data))
    return 1;

  switch (TREE_CODE (t))
    {
    case INDIRECT_REF:
    case COMPONENT_REF:
      /* We assume that the object must be instantiated in order to build
	 the COMPONENT_REF, so we test only whether the type of the
	 COMPONENT_REF uses template parms.  */
      return for_each_template_parm (TREE_TYPE (t), fn, data);

    case IDENTIFIER_NODE:
      if (!IDENTIFIER_TEMPLATE (t))
	return 0;
      my_friendly_abort (42);

      /* aggregates of tree nodes */
    case TREE_VEC:
      {
	int i = TREE_VEC_LENGTH (t);
	while (i--)
	  if (for_each_template_parm (TREE_VEC_ELT (t, i), fn, data))
	    return 1;
	return 0;
      }
    case TREE_LIST:
      if (for_each_template_parm (TREE_PURPOSE (t), fn, data)
	  || for_each_template_parm (TREE_VALUE (t), fn, data))
	return 1;
      return for_each_template_parm (TREE_CHAIN (t), fn, data);

    case OVERLOAD:
      if (for_each_template_parm (OVL_FUNCTION (t), fn, data))
	return 1;
      return for_each_template_parm (OVL_CHAIN (t), fn, data);

      /* constructed type nodes */
    case POINTER_TYPE:
    case REFERENCE_TYPE:
      return for_each_template_parm (TREE_TYPE (t), fn, data);

    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_FLAG (t))
	return for_each_template_parm (TYPE_PTRMEMFUNC_FN_TYPE (t),
				       fn, data);
      /* Fall through.  */

    case UNION_TYPE:
    case ENUMERAL_TYPE:
      if (! TYPE_TEMPLATE_INFO (t))
	return 0;
      return for_each_template_parm (TREE_VALUE
				     (TYPE_TEMPLATE_INFO (t)),
				     fn, data);
    case FUNCTION_TYPE:
      if (for_each_template_parm (TYPE_ARG_TYPES (t), fn, data))
	return 1;
      return for_each_template_parm (TREE_TYPE (t), fn, data);
    case ARRAY_TYPE:
      if (for_each_template_parm (TYPE_DOMAIN (t), fn, data))
	return 1;
      return for_each_template_parm (TREE_TYPE (t), fn, data);
    case OFFSET_TYPE:
      if (for_each_template_parm (TYPE_OFFSET_BASETYPE (t), fn, data))
	return 1;
      return for_each_template_parm (TREE_TYPE (t), fn, data);
    case METHOD_TYPE:
      if (for_each_template_parm (TYPE_METHOD_BASETYPE (t), fn, data))
	return 1;
      if (for_each_template_parm (TYPE_ARG_TYPES (t), fn, data))
	return 1;
      return for_each_template_parm (TREE_TYPE (t), fn, data);

      /* decl nodes */
    case TYPE_DECL:
      return for_each_template_parm (TREE_TYPE (t), fn, data);

    case TEMPLATE_DECL:
      /* A template template parameter is encountered */
      if (DECL_TEMPLATE_TEMPLATE_PARM_P (t))
	return for_each_template_parm (TREE_TYPE (t), fn, data);
      /* Already substituted template template parameter */
      return 0;
      
    case CONST_DECL:
      if (for_each_template_parm (DECL_INITIAL (t), fn, data))
	return 1;
      goto check_type_and_context;

    case FUNCTION_DECL:
    case VAR_DECL:
      /* ??? What about FIELD_DECLs?  */
      if (DECL_LANG_SPECIFIC (t) && DECL_TEMPLATE_INFO (t)
	  && for_each_template_parm (DECL_TI_ARGS (t), fn, data))
	return 1;
      /* fall through */
    case PARM_DECL:
    check_type_and_context:
      if (for_each_template_parm (TREE_TYPE (t), fn, data))
	return 1;
      if (DECL_CONTEXT (t) 
	  && for_each_template_parm (DECL_CONTEXT (t), fn, data))
	return 1;
      return 0;

    case CALL_EXPR:
      return for_each_template_parm (TREE_TYPE (t), fn, data);
    case ADDR_EXPR:
      return for_each_template_parm (TREE_OPERAND (t, 0), fn, data);

      /* template parm nodes */
    case TEMPLATE_TEMPLATE_PARM:
      /* Record template parameters such as `T' inside `TT<T>'.  */
      if (CLASSTYPE_TEMPLATE_INFO (t)
	  && for_each_template_parm (CLASSTYPE_TI_ARGS (t), fn, data))
	return 1;
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_PARM_INDEX:
      if (fn)
	return (*fn)(t, data);
      else
	return 1;

      /* simple type nodes */
    case INTEGER_TYPE:
      if (for_each_template_parm (TYPE_MIN_VALUE (t), fn, data))
	return 1;
      return for_each_template_parm (TYPE_MAX_VALUE (t), fn, data);

    case REAL_TYPE:
    case COMPLEX_TYPE:
    case VOID_TYPE:
    case BOOLEAN_TYPE:
    case NAMESPACE_DECL:
      return 0;

      /* constants */
    case INTEGER_CST:
    case REAL_CST:
    case STRING_CST:
      return 0;

    case ERROR_MARK:
      /* Non-error_mark_node ERROR_MARKs are bad things.  */
      my_friendly_assert (t == error_mark_node, 274);
      /* NOTREACHED */
      return 0;

    case LOOKUP_EXPR:
    case TYPENAME_TYPE:
      return 1;

    case SCOPE_REF:
      return for_each_template_parm (TREE_OPERAND (t, 0), fn, data);

    case CONSTRUCTOR:
      if (TREE_TYPE (t) && TYPE_PTRMEMFUNC_P (TREE_TYPE (t)))
	return for_each_template_parm (TYPE_PTRMEMFUNC_FN_TYPE
				       (TREE_TYPE (t)), fn, data);
      return for_each_template_parm (TREE_OPERAND (t, 1), fn, data);

    case MODOP_EXPR:
    case CAST_EXPR:
    case REINTERPRET_CAST_EXPR:
    case CONST_CAST_EXPR:
    case STATIC_CAST_EXPR:
    case DYNAMIC_CAST_EXPR:
    case ARROW_EXPR:
    case DOTSTAR_EXPR:
    case TYPEID_EXPR:
      return 1;

    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
      return for_each_template_parm (TREE_OPERAND (t, 0), fn, data);

    default:
      switch (TREE_CODE_CLASS (TREE_CODE (t)))
	{
	case '1':
	case '2':
	case 'e':
	case '<':
	  {
	    int i;
	    for (i = first_rtl_op (TREE_CODE (t)); --i >= 0;)
	      if (for_each_template_parm (TREE_OPERAND (t, i), fn, data))
		return 1;
	    return 0;
	  }
	default:
	  break;
	}
      sorry ("testing %s for template parms",
	     tree_code_name [(int) TREE_CODE (t)]);
      my_friendly_abort (82);
      /* NOTREACHED */
      return 0;
    }
}

int
uses_template_parms (t)
     tree t;
{
  return for_each_template_parm (t, 0, 0);
}

static struct tinst_level *current_tinst_level;
static struct tinst_level *free_tinst_level;
static int tinst_depth;
extern int max_tinst_depth;
#ifdef GATHER_STATISTICS
int depth_reached;
#endif
int tinst_level_tick;
int last_template_error_tick;

/* Print out all the template instantiations that we are currently
   working on.  If ERR, we are being called from cp_thing, so do
   the right thing for an error message.  */

static void
print_template_context (err)
     int err;
{
  struct tinst_level *p = current_tinst_level;
  int line = lineno;
  char *file = input_filename;

  if (err && p)
    {
      if (current_function_decl != p->decl
	  && current_function_decl != NULL_TREE)
	/* We can get here during the processing of some synthesized
	   method.  Then, p->decl will be the function that's causing
	   the synthesis.  */
	;
      else
	{
	  if (current_function_decl == p->decl)
	    /* Avoid redundancy with the the "In function" line.  */;
	  else 
	    fprintf (stderr, "%s: In instantiation of `%s':\n",
		     file, decl_as_string (p->decl, 0));
	  
	  line = p->line;
	  file = p->file;
	  p = p->next;
	}
    }

  for (; p; p = p->next)
    {
      fprintf (stderr, "%s:%d:   instantiated from `%s'\n", file, line,
	       decl_as_string (p->decl, 0));
      line = p->line;
      file = p->file;
    }
  fprintf (stderr, "%s:%d:   instantiated from here\n", file, line);
}

/* Called from cp_thing to print the template context for an error.  */

void
maybe_print_template_context ()
{
  if (last_template_error_tick == tinst_level_tick
      || current_tinst_level == 0)
    return;

  last_template_error_tick = tinst_level_tick;
  print_template_context (1);
}

static int
push_tinst_level (d)
     tree d;
{
  struct tinst_level *new;

  if (tinst_depth >= max_tinst_depth)
    {
      /* If the instantiation in question still has unbound template parms,
	 we don't really care if we can't instantiate it, so just return.
         This happens with base instantiation for implicit `typename'.  */
      if (uses_template_parms (d))
	return 0;

      last_template_error_tick = tinst_level_tick;
      error ("template instantiation depth exceeds maximum of %d",
	     max_tinst_depth);
      error (" (use -ftemplate-depth-NN to increase the maximum)");
      cp_error ("  instantiating `%D'", d);

      print_template_context (0);

      return 0;
    }

  if (free_tinst_level)
    {
      new = free_tinst_level;
      free_tinst_level = new->next;
    }
  else
    new = (struct tinst_level *) xmalloc (sizeof (struct tinst_level));

  new->decl = d;
  new->line = lineno;
  new->file = input_filename;
  new->next = current_tinst_level;
  current_tinst_level = new;

  ++tinst_depth;
#ifdef GATHER_STATISTICS
  if (tinst_depth > depth_reached)
    depth_reached = tinst_depth;
#endif

  ++tinst_level_tick;
  return 1;
}

void
pop_tinst_level ()
{
  struct tinst_level *old = current_tinst_level;

  /* Restore the filename and line number stashed away when we started
     this instantiation.  */
  lineno = old->line;
  input_filename = old->file;
  
  current_tinst_level = old->next;
  old->next = free_tinst_level;
  free_tinst_level = old;
  --tinst_depth;
  ++tinst_level_tick;
}

struct tinst_level *
tinst_for_decl ()
{
  struct tinst_level *p = current_tinst_level;

  if (p)
    for (; p->next ; p = p->next )
      ;
  return p;
}

/* DECL is a friend FUNCTION_DECL or TEMPLATE_DECL.  ARGS is the
   vector of template arguments, as for tsubst.

   Returns an appropriate tsbust'd friend declaration.  */

static tree
tsubst_friend_function (decl, args)
     tree decl;
     tree args;
{
  tree new_friend;
  int line = lineno;
  char *file = input_filename;

  lineno = DECL_SOURCE_LINE (decl);
  input_filename = DECL_SOURCE_FILE (decl);

  if (TREE_CODE (decl) == FUNCTION_DECL 
      && DECL_TEMPLATE_INSTANTIATION (decl)
      && TREE_CODE (DECL_TI_TEMPLATE (decl)) != TEMPLATE_DECL)
    /* This was a friend declared with an explicit template
       argument list, e.g.:
       
       friend void f<>(T);
       
       to indicate that f was a template instantiation, not a new
       function declaration.  Now, we have to figure out what
       instantiation of what template.  */
    {
      tree template_id;
      tree new_args;
      tree tmpl;

      template_id
	= lookup_template_function (tsubst_expr (DECL_TI_TEMPLATE (decl),
						 args, NULL_TREE),
				    tsubst (DECL_TI_ARGS (decl),
					    args, NULL_TREE));
      /* FIXME: The decl we create via the next tsubst could be
	 created on a temporary obstack.  */
      new_friend = tsubst (decl, args, NULL_TREE);
      tmpl = determine_specialization (template_id, new_friend,
				       &new_args, 
				       /*need_member_template=*/0, 
				       /*complain=*/1);
      new_friend = instantiate_template (tmpl, new_args);
      goto done;
    }

  new_friend = tsubst (decl, args, NULL_TREE);
	
  /* The NEW_FRIEND will look like an instantiation, to the
     compiler, but is not an instantiation from the point of view of
     the language.  For example, we might have had:
     
     template <class T> struct S {
       template <class U> friend void f(T, U);
     };
     
     Then, in S<int>, template <class U> void f(int, U) is not an
     instantiation of anything.  */
  DECL_USE_TEMPLATE (new_friend) = 0;
  if (TREE_CODE (decl) == TEMPLATE_DECL)
    DECL_USE_TEMPLATE (DECL_TEMPLATE_RESULT (new_friend)) = 0;

  /* The mangled name for the NEW_FRIEND is incorrect.  The call to
     tsubst will have resulted in a call to
     set_mangled_name_for_template_decl.  But, the function is not a
     template instantiation and should not be mangled like one.
     Therefore, we remangle the function name.  We don't have to do
     this if the NEW_FRIEND is a template since
     set_mangled_name_for_template_decl doesn't do anything if the
     function declaration still uses template arguments.  */
  if (TREE_CODE (new_friend) != TEMPLATE_DECL)
    {
      set_mangled_name_for_decl (new_friend);
      DECL_RTL (new_friend) = 0;
      make_decl_rtl (new_friend, NULL_PTR, 1);
    }
      
  if (DECL_NAMESPACE_SCOPE_P (new_friend))
    {
      tree old_decl;
      tree new_friend_args;

      if (TREE_CODE (new_friend) == TEMPLATE_DECL)
	/* This declaration is a `primary' template.  */
	DECL_PRIMARY_TEMPLATE (new_friend) = new_friend;

      /* We must save the DECL_TI_ARGS for NEW_FRIEND here because
	 pushdecl may call duplicate_decls which will free NEW_FRIEND
	 if possible.  */
      new_friend_args = DECL_TI_ARGS (new_friend);
      old_decl = pushdecl_namespace_level (new_friend);

      if (old_decl != new_friend)
	{
	  /* This new friend declaration matched an existing
	     declaration.  For example, given:

	       template <class T> void f(T);
	       template <class U> class C { 
		 template <class T> friend void f(T) {} 
	       };

	     the friend declaration actually provides the definition
	     of `f', once C has been instantiated for some type.  So,
	     old_decl will be the out-of-class template declaration,
	     while new_friend is the in-class definition.

	     But, if `f' was called before this point, the
	     instantiation of `f' will have DECL_TI_ARGS corresponding
	     to `T' but not to `U', references to which might appear
	     in the definition of `f'.  Previously, the most general
	     template for an instantiation of `f' was the out-of-class
	     version; now it is the in-class version.  Therefore, we
	     run through all specialization of `f', adding to their
	     DECL_TI_ARGS appropriately.  In particular, they need a
	     new set of outer arguments, corresponding to the
	     arguments for this class instantiation.  

	     The same situation can arise with something like this:

	       friend void f(int);
	       template <class T> class C { 
	         friend void f(T) {}
               };

	     when `C<int>' is instantiated.  Now, `f(int)' is defined
	     in the class.  */

	  if (TREE_CODE (old_decl) != TEMPLATE_DECL)
	    /* duplicate_decls will take care of this case.  */
	    ;
	  else 
	    {
	      tree t;

	      for (t = DECL_TEMPLATE_SPECIALIZATIONS (old_decl); 
		   t != NULL_TREE;
		   t = TREE_CHAIN (t))
		{
		  tree spec = TREE_VALUE (t);
		  
		  DECL_TI_ARGS (spec) 
		    = add_outermost_template_args (new_friend_args,
						   DECL_TI_ARGS (spec));
		  DECL_TI_ARGS (spec)
		    = copy_to_permanent (DECL_TI_ARGS (spec));
		}

	      /* Now, since specializations are always supposed to
		 hang off of the most general template, we must move
		 them.  */
	      t = most_general_template (old_decl);
	      if (t != old_decl)
		{
		  DECL_TEMPLATE_SPECIALIZATIONS (t)
		    = chainon (DECL_TEMPLATE_SPECIALIZATIONS (t),
			       DECL_TEMPLATE_SPECIALIZATIONS (old_decl));
		  DECL_TEMPLATE_SPECIALIZATIONS (old_decl) = NULL_TREE;
		}
	    }

	  /* The information from NEW_FRIEND has been merged into OLD_DECL
	     by duplicate_decls.  */
	  new_friend = old_decl;
	}
    }
  else if (TYPE_SIZE (DECL_CONTEXT (new_friend)))
    {
      /* Check to see that the declaration is really present, and,
	 possibly obtain an improved declaration.  */
      tree fn = check_classfn (DECL_CONTEXT (new_friend),
			       new_friend);
      
      if (fn)
	new_friend = fn;
    }

 done:
  lineno = line;
  input_filename = file;
  return new_friend;
}

/* FRIEND_TMPL is a friend TEMPLATE_DECL.  ARGS is the vector of
   template arguments, as for tsubst.

   Returns an appropriate tsbust'd friend type.  */

static tree
tsubst_friend_class (friend_tmpl, args)
     tree friend_tmpl;
     tree args;
{
  tree friend_type;
  tree tmpl = lookup_name (DECL_NAME (friend_tmpl), 1); 

  tmpl = maybe_get_template_decl_from_type_decl (tmpl);

  if (tmpl != NULL_TREE && DECL_CLASS_TEMPLATE_P (tmpl))
    {
      /* The friend template has already been declared.  Just
	 check to see that the declarations match, and install any new
	 default parameters.  We must tsubst the default parameters,
	 of course.  We only need the innermost template parameters
	 because that is all that redeclare_class_template will look
	 at.  */
      tree parms 
	= tsubst_template_parms (DECL_TEMPLATE_PARMS (friend_tmpl),
				 args);
      redeclare_class_template (TREE_TYPE (tmpl), parms);
      friend_type = TREE_TYPE (tmpl);
    }
  else
    {
      /* The friend template has not already been declared.  In this
	 case, the instantiation of the template class will cause the
	 injection of this template into the global scope.  */
      tmpl = tsubst (friend_tmpl, args, NULL_TREE);

      /* The new TMPL is not an instantiation of anything, so we
 	 forget its origins.  We don't reset CLASSTYPE_TI_TEMPLATE for
	 the new type because that is supposed to be the corresponding
	 template decl, i.e., TMPL.  */
      DECL_USE_TEMPLATE (tmpl) = 0;
      DECL_TEMPLATE_INFO (tmpl) = NULL_TREE;
      CLASSTYPE_USE_TEMPLATE (TREE_TYPE (tmpl)) = 0;

      /* Inject this template into the global scope.  */
      friend_type = TREE_TYPE (pushdecl_top_level (tmpl));
    }

  return friend_type;
}

tree
instantiate_class_template (type)
     tree type;
{
  tree template, args, pattern, t, *field_chain;
  tree typedecl;

  if (type == error_mark_node)
    return error_mark_node;

  if (TYPE_BEING_DEFINED (type) || TYPE_SIZE (type))
    return type;

  template = most_general_template (CLASSTYPE_TI_TEMPLATE (type));
  args = CLASSTYPE_TI_ARGS (type);
  my_friendly_assert (TREE_CODE (template) == TEMPLATE_DECL, 279);
  t = most_specialized_class (template, args);

  if (t == error_mark_node)
    {
      char *str = "candidates are:";
      cp_error ("ambiguous class template instantiation for `%#T'", type);
      for (t = DECL_TEMPLATE_SPECIALIZATIONS (template); t; t = TREE_CHAIN (t))
	{
	  if (get_class_bindings (TREE_VALUE (t), TREE_PURPOSE (t),
				  args))
	    {
	      cp_error_at ("%s %+#T", str, TREE_TYPE (t));
	      str = "               ";
	    }
	}
      TYPE_BEING_DEFINED (type) = 1;
      return error_mark_node;
    }
  else if (t)
    pattern = TREE_TYPE (t);
  else
    pattern = TREE_TYPE (template);

  if (TYPE_SIZE (pattern) == NULL_TREE)
    return type;

  if (t)
    {
      /* This TYPE is actually a instantiation of of a partial
	 specialization.  We replace the innermost set of ARGS with
	 the arguments appropriate for substitution.  For example,
	 given:

	   template <class T> struct S {};
	   template <class T> struct S<T*> {};
	 
	 and supposing that we are instantiating S<int*>, ARGS will
	 present be {int*} but we need {int}.  */
      tree inner_args 
	= get_class_bindings (TREE_VALUE (t), TREE_PURPOSE (t),
			      args);

      /* If there were multiple levels in ARGS, replacing the
	 innermost level would alter CLASSTYPE_TI_ARGS, which we don't
	 want, so we make a copy first.  */
      if (TMPL_ARGS_HAVE_MULTIPLE_LEVELS (args))
	{
	  args = copy_node (args);
	  SET_TMPL_ARGS_LEVEL (args, TMPL_ARGS_DEPTH (args), inner_args);
	}
      else
	args = inner_args;
    }

  if (pedantic && uses_template_parms (args))
    /* If there are still template parameters amongst the args, then
       we can't instantiate the type; there's no telling whether or not one
       of the template parameters might eventually be instantiated to some
       value that results in a specialization being used.  */
    return type;

  TYPE_BEING_DEFINED (type) = 1;

  if (! push_tinst_level (type))
    return type;

  maybe_push_to_top_level (uses_template_parms (type));
  pushclass (type, 0);

  /* We must copy the arguments to the permanent obstack since
     during the tsubst'ing below they may wind up in the
     DECL_TI_ARGS of some instantiated member template.  */
  args = copy_to_permanent (args);

  if (flag_external_templates)
    {
      if (flag_alt_external_templates)
	{
	  CLASSTYPE_INTERFACE_ONLY (type) = interface_only;
	  SET_CLASSTYPE_INTERFACE_UNKNOWN_X (type, interface_unknown);
	  CLASSTYPE_VTABLE_NEEDS_WRITING (type)
	    = (! CLASSTYPE_INTERFACE_ONLY (type)
	       && CLASSTYPE_INTERFACE_KNOWN (type));
	}
      else
	{
	  CLASSTYPE_INTERFACE_ONLY (type) = CLASSTYPE_INTERFACE_ONLY (pattern);
	  SET_CLASSTYPE_INTERFACE_UNKNOWN_X
	    (type, CLASSTYPE_INTERFACE_UNKNOWN (pattern));
	  CLASSTYPE_VTABLE_NEEDS_WRITING (type)
	    = (! CLASSTYPE_INTERFACE_ONLY (type)
	       && CLASSTYPE_INTERFACE_KNOWN (type));
	}
    }
  else
    {
      SET_CLASSTYPE_INTERFACE_UNKNOWN (type);
      CLASSTYPE_VTABLE_NEEDS_WRITING (type) = 1;
    }

  TYPE_HAS_CONSTRUCTOR (type) = TYPE_HAS_CONSTRUCTOR (pattern);
  TYPE_HAS_DESTRUCTOR (type) = TYPE_HAS_DESTRUCTOR (pattern);
  TYPE_HAS_ASSIGNMENT (type) = TYPE_HAS_ASSIGNMENT (pattern);
  TYPE_OVERLOADS_CALL_EXPR (type) = TYPE_OVERLOADS_CALL_EXPR (pattern);
  TYPE_OVERLOADS_ARRAY_REF (type) = TYPE_OVERLOADS_ARRAY_REF (pattern);
  TYPE_OVERLOADS_ARROW (type) = TYPE_OVERLOADS_ARROW (pattern);
  TYPE_GETS_NEW (type) = TYPE_GETS_NEW (pattern);
  TYPE_GETS_DELETE (type) = TYPE_GETS_DELETE (pattern);
  TYPE_VEC_DELETE_TAKES_SIZE (type) = TYPE_VEC_DELETE_TAKES_SIZE (pattern);
  TYPE_HAS_ASSIGN_REF (type) = TYPE_HAS_ASSIGN_REF (pattern);
  TYPE_HAS_CONST_ASSIGN_REF (type) = TYPE_HAS_CONST_ASSIGN_REF (pattern);
  TYPE_HAS_ABSTRACT_ASSIGN_REF (type) = TYPE_HAS_ABSTRACT_ASSIGN_REF (pattern);
  TYPE_HAS_INIT_REF (type) = TYPE_HAS_INIT_REF (pattern);
  TYPE_HAS_CONST_INIT_REF (type) = TYPE_HAS_CONST_INIT_REF (pattern);
  TYPE_HAS_DEFAULT_CONSTRUCTOR (type) = TYPE_HAS_DEFAULT_CONSTRUCTOR (pattern);
  TYPE_HAS_CONVERSION (type) = TYPE_HAS_CONVERSION (pattern);
  TYPE_USES_COMPLEX_INHERITANCE (type)
    = TYPE_USES_COMPLEX_INHERITANCE (pattern);
  TYPE_USES_MULTIPLE_INHERITANCE (type)
    = TYPE_USES_MULTIPLE_INHERITANCE (pattern);
  TYPE_USES_VIRTUAL_BASECLASSES (type)
    = TYPE_USES_VIRTUAL_BASECLASSES (pattern);
  TYPE_PACKED (type) = TYPE_PACKED (pattern);
  TYPE_ALIGN (type) = TYPE_ALIGN (pattern);
  TYPE_FOR_JAVA (type) = TYPE_FOR_JAVA (pattern); /* For libjava's JArray<T> */

  CLASSTYPE_LOCAL_TYPEDECLS (type) = CLASSTYPE_LOCAL_TYPEDECLS (pattern);

  /* If this is a partial instantiation, don't tsubst anything.  We will
     only use this type for implicit typename, so the actual contents don't
     matter.  All that matters is whether a particular name is a type.  */
  if (uses_template_parms (type))
    {
      TYPE_BINFO_BASETYPES (type) = TYPE_BINFO_BASETYPES (pattern);
      TYPE_FIELDS (type) = TYPE_FIELDS (pattern);
      TYPE_METHODS (type) = TYPE_METHODS (pattern);
      CLASSTYPE_TAGS (type) = CLASSTYPE_TAGS (pattern);
      TYPE_SIZE (type) = integer_zero_node;
      goto end;
    }

  {
    tree binfo = TYPE_BINFO (type);
    tree pbases = TYPE_BINFO_BASETYPES (pattern);

    if (pbases)
      {
	tree bases;
	int i;
	int len = TREE_VEC_LENGTH (pbases);
	bases = make_tree_vec (len);
	for (i = 0; i < len; ++i)
	  {
	    tree elt, basetype;

	    TREE_VEC_ELT (bases, i) = elt
	      = tsubst (TREE_VEC_ELT (pbases, i), args, NULL_TREE);
	    BINFO_INHERITANCE_CHAIN (elt) = binfo;

	    basetype = TREE_TYPE (elt);

	    if (! IS_AGGR_TYPE (basetype))
	      cp_error
		("base type `%T' of `%T' fails to be a struct or class type",
		 basetype, type);
	    else if (TYPE_SIZE (complete_type (basetype)) == NULL_TREE)
	      cp_error ("base class `%T' of `%T' has incomplete type",
			basetype, type);

	    /* These are set up in xref_basetypes for normal classes, so
	       we have to handle them here for template bases.  */
	    if (TYPE_USES_VIRTUAL_BASECLASSES (basetype))
	      {
		TYPE_USES_VIRTUAL_BASECLASSES (type) = 1;
		TYPE_USES_COMPLEX_INHERITANCE (type) = 1;
	      }
	    TYPE_GETS_NEW (type) |= TYPE_GETS_NEW (basetype);
	    TYPE_GETS_DELETE (type) |= TYPE_GETS_DELETE (basetype);
	    CLASSTYPE_LOCAL_TYPEDECLS (type)
	      |= CLASSTYPE_LOCAL_TYPEDECLS (basetype);
	  }
	/* Don't initialize this until the vector is filled out, or
	   lookups will crash.  */
	BINFO_BASETYPES (binfo) = bases;
      }
  }

  field_chain = &TYPE_FIELDS (type);

  for (t = CLASSTYPE_TAGS (pattern); t; t = TREE_CHAIN (t))
    {
      tree tag = TREE_VALUE (t);
      tree name = TYPE_IDENTIFIER (tag);
      tree newtag;

      newtag = tsubst (tag, args, NULL_TREE);
      if (TREE_CODE (newtag) == ENUMERAL_TYPE)
	{
	  extern tree current_local_enum;
	  tree prev_local_enum = current_local_enum;

	  if (TYPE_VALUES (newtag))
	    {
	      tree v;

	      /* We must set things up so that CURRENT_LOCAL_ENUM is the
		 CONST_DECL for the last enumeration constant, since the
		 CONST_DECLs are chained backwards.  */
	      for (v = TYPE_VALUES (newtag); TREE_CHAIN (v); 
		   v = TREE_CHAIN (v))
		;

	      current_local_enum 
		= IDENTIFIER_CLASS_VALUE (TREE_PURPOSE (v));
	      *field_chain = grok_enum_decls (NULL_TREE);
	      current_local_enum = prev_local_enum;

	      while (*field_chain)
		{
		  DECL_FIELD_CONTEXT (*field_chain) = type;
		  field_chain = &TREE_CHAIN (*field_chain);
		}
	    }
	}
      else
	{
	  /* Now, we call pushtag to put this NEWTAG into the scope of
	     TYPE.  We first set up the IDENTIFIER_TYPE_VALUE to avoid
	     pushtag calling push_template_decl.  We don't have to do
	     this for enums because it will already have been done in
	     tsubst_enum.  */
	  if (name)
	    SET_IDENTIFIER_TYPE_VALUE (name, newtag);
	  pushtag (name, newtag, /*globalize=*/0);
	}
    }

  /* Don't replace enum constants here.  */
  for (t = TYPE_FIELDS (pattern); t; t = TREE_CHAIN (t))
    if (TREE_CODE (t) != CONST_DECL)
      {
	tree r;

	/* The the file and line for this declaration, to assist in
	   error message reporting.  Since we called push_tinst_level
	   above, we don't need to restore these.  */
	lineno = DECL_SOURCE_LINE (t);
	input_filename = DECL_SOURCE_FILE (t);

	r = tsubst (t, args, NULL_TREE);
	if (TREE_CODE (r) == VAR_DECL)
	  {
	    pending_statics = perm_tree_cons (NULL_TREE, r, pending_statics);
	    /* Perhaps we should do more of grokfield here.  */
	    start_decl_1 (r);
	    DECL_IN_AGGR_P (r) = 1;
	    DECL_EXTERNAL (r) = 1;
	    cp_finish_decl (r, DECL_INITIAL (r), NULL_TREE, 0, 0);
	  }

	*field_chain = r;
	field_chain = &TREE_CHAIN (r);
      }

  TYPE_METHODS (type) = tsubst_chain (TYPE_METHODS (pattern), args);

  /* Construct the DECL_FRIENDLIST for the new class type.  */
  typedecl = TYPE_MAIN_DECL (type);
  for (t = DECL_FRIENDLIST (TYPE_MAIN_DECL (pattern));
       t != NULL_TREE;
       t = TREE_CHAIN (t))
    {
      tree friends;

      DECL_FRIENDLIST (typedecl)
	= tree_cons (TREE_PURPOSE (t), NULL_TREE, 
		     DECL_FRIENDLIST (typedecl));

      for (friends = TREE_VALUE (t);
	   friends != NULL_TREE;
	   friends = TREE_CHAIN (friends))
	{
	  if (TREE_PURPOSE (friends) == error_mark_node)
	    {
	      TREE_VALUE (DECL_FRIENDLIST (typedecl))
		= tree_cons (error_mark_node, 
			     tsubst_friend_function (TREE_VALUE (friends),
						     args),
			     TREE_VALUE (DECL_FRIENDLIST (typedecl)));
	    }
	  else
	    {
	      TREE_VALUE (DECL_FRIENDLIST (typedecl))
		= tree_cons (tsubst (TREE_PURPOSE (friends), args, NULL_TREE),
			     NULL_TREE,
			     TREE_VALUE (DECL_FRIENDLIST (typedecl)));

	    }
	}
    }

  for (t = CLASSTYPE_FRIEND_CLASSES (pattern);
       t != NULL_TREE;
       t = TREE_CHAIN (t))
    {
      tree friend_type = TREE_VALUE (t);
      tree new_friend_type;

      if (TREE_CODE (friend_type) == TEMPLATE_DECL)
	new_friend_type = tsubst_friend_class (friend_type, args);
      else if (uses_template_parms (friend_type))
	new_friend_type = tsubst (friend_type, args, NULL_TREE);
      else 
	/* The call to xref_tag_from_type does injection for friend
	   classes.  */
	new_friend_type = 
	  xref_tag_from_type (friend_type, NULL_TREE, 1);


      if (TREE_CODE (friend_type) == TEMPLATE_DECL)
	/* Trick make_friend_class into realizing that the friend
	   we're adding is a template, not an ordinary class.  It's
	   important that we use make_friend_class since it will
	   perform some error-checking and output cross-reference
	   information.  */
	++processing_template_decl;

      make_friend_class (type, new_friend_type);

      if (TREE_CODE (friend_type) == TEMPLATE_DECL)
	--processing_template_decl;
    }

  /* This does injection for friend functions. */
  if (!processing_template_decl)
    {
      t = tsubst (DECL_TEMPLATE_INJECT (template), args, NULL_TREE);

      for (; t; t = TREE_CHAIN (t))
	{
	  tree d = TREE_VALUE (t);

	  if (TREE_CODE (d) == TYPE_DECL)
	    /* Already injected.  */;
	  else
	    pushdecl (d);
	}
    } 

  for (t = TYPE_FIELDS (type); t; t = TREE_CHAIN (t))
    if (TREE_CODE (t) == FIELD_DECL)
      {
	TREE_TYPE (t) = complete_type (TREE_TYPE (t));
	require_complete_type (t);
      }

  type = finish_struct_1 (type, 0);
  CLASSTYPE_GOT_SEMICOLON (type) = 1;

  repo_template_used (type);
  if (at_eof && TYPE_BINFO_VTABLE (type) != NULL_TREE)
    finish_prevtable_vardecl (NULL, TYPE_BINFO_VTABLE (type));

 end:
  TYPE_BEING_DEFINED (type) = 0;
  popclass (0);

  pop_from_top_level ();
  pop_tinst_level ();

  return type;
}

static int
list_eq (t1, t2)
     tree t1, t2;
{
  if (t1 == NULL_TREE)
    return t2 == NULL_TREE;
  if (t2 == NULL_TREE)
    return 0;
  /* Don't care if one declares its arg const and the other doesn't -- the
     main variant of the arg type is all that matters.  */
  if (TYPE_MAIN_VARIANT (TREE_VALUE (t1))
      != TYPE_MAIN_VARIANT (TREE_VALUE (t2)))
    return 0;
  return list_eq (TREE_CHAIN (t1), TREE_CHAIN (t2));
}

/* If arg is a non-type template parameter that does not depend on template
   arguments, fold it like we weren't in the body of a template.  */

static tree
maybe_fold_nontype_arg (arg)
     tree arg;
{
  if (TREE_CODE_CLASS (TREE_CODE (arg)) != 't'
      && !uses_template_parms (arg))
    {
      /* Sometimes, one of the args was an expression involving a
	 template constant parameter, like N - 1.  Now that we've
	 tsubst'd, we might have something like 2 - 1.  This will
	 confuse lookup_template_class, so we do constant folding
	 here.  We have to unset processing_template_decl, to
	 fool build_expr_from_tree() into building an actual
	 tree.  */

      int saved_processing_template_decl = processing_template_decl; 
      processing_template_decl = 0;
      arg = fold (build_expr_from_tree (arg));
      processing_template_decl = saved_processing_template_decl; 
    }
  return arg;
}

/* Return the TREE_VEC with the arguments for the innermost template header,
   where ARGS is either that or the VEC of VECs for all the
   arguments.  */

tree
innermost_args (args)
     tree args;
{
  return TMPL_ARGS_LEVEL (args, TMPL_ARGS_DEPTH (args));
}

/* Substitute ARGS into the vector of template arguments T.  */

tree
tsubst_template_arg_vector (t, args)
     tree t;
     tree args;
{
  int len = TREE_VEC_LENGTH (t), need_new = 0, i;
  tree *elts = (tree *) alloca (len * sizeof (tree));
  
  bzero ((char *) elts, len * sizeof (tree));
  
  for (i = 0; i < len; i++)
    {
      if (TREE_VEC_ELT (t, i) != NULL_TREE
	  && TREE_CODE (TREE_VEC_ELT (t, i)) == TREE_VEC)
	elts[i] = tsubst_template_arg_vector (TREE_VEC_ELT (t, i), args);
      else
	elts[i] = maybe_fold_nontype_arg
	  (tsubst_expr (TREE_VEC_ELT (t, i), args, NULL_TREE));
      
      if (elts[i] != TREE_VEC_ELT (t, i))
	need_new = 1;
    }
  
  if (!need_new)
    return t;
  
  t = make_tree_vec (len);
  for (i = 0; i < len; i++)
    TREE_VEC_ELT (t, i) = elts[i];
  
  return t;
}

/* Return the result of substituting ARGS into the template parameters
   given by PARMS.  If there are m levels of ARGS and m + n levels of
   PARMS, then the result will contain n levels of PARMS.  For
   example, if PARMS is `template <class T> template <class U>
   template <T*, U, class V>' and ARGS is {{int}, {double}} then the
   result will be `template <int*, double, class V>'.  */

tree
tsubst_template_parms (parms, args)
     tree parms;
     tree args;
{
  tree r;
  tree* new_parms = &r;

  for (new_parms = &r;
       TMPL_PARMS_DEPTH (parms) > TMPL_ARGS_DEPTH (args);
       new_parms = &(TREE_CHAIN (*new_parms)),
	 parms = TREE_CHAIN (parms))
    {
      tree new_vec = 
	make_tree_vec (TREE_VEC_LENGTH (TREE_VALUE (parms)));
      int i;
      
      for (i = 0; i < TREE_VEC_LENGTH (new_vec); ++i)
	{
	  tree default_value =
	    TREE_PURPOSE (TREE_VEC_ELT (TREE_VALUE (parms), i));
	  tree parm_decl = 
	    TREE_VALUE (TREE_VEC_ELT (TREE_VALUE (parms), i));
	  
	  TREE_VEC_ELT (new_vec, i)
	    = build_tree_list (tsubst (default_value, args, NULL_TREE),
			       tsubst (parm_decl, args, NULL_TREE));
	  
	}
      
      *new_parms = 
	tree_cons (build_int_2 (0, (TMPL_PARMS_DEPTH (parms) 
				    - TMPL_ARGS_DEPTH (args))),
		   new_vec, NULL_TREE);
    }

  return r;
}

/* Substitute the ARGS into the indicated aggregate (or enumeration)
   type T.  If T is not an aggregate or enumeration type, it is
   handled as if by tsubst.  IN_DECL is as for tsubst.  If
   ENTERING_SCOPE is non-zero, T is the context for a template which
   we are presently tsubst'ing.  Return the subsituted value.  */

tree
tsubst_aggr_type (t, args, in_decl, entering_scope)
     tree t;
     tree args;
     tree in_decl;
     int entering_scope;
{
  if (t == NULL_TREE)
    return NULL_TREE;

  switch (TREE_CODE (t))
    {
    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_P (t))
	{
	  tree r = build_ptrmemfunc_type
	    (tsubst (TYPE_PTRMEMFUNC_FN_TYPE (t), args, in_decl));
	  return cp_build_type_variant (r, TYPE_READONLY (t),
					TYPE_VOLATILE (t));
	}

      /* else fall through */
    case ENUMERAL_TYPE:
    case UNION_TYPE:
      if (uses_template_parms (t))
	{
	  tree argvec;
	  tree context;
	  tree r;

	  /* First, determine the context for the type we are looking
	     up.  */
	  if (TYPE_CONTEXT (t) != NULL_TREE)
	    context = tsubst_aggr_type (TYPE_CONTEXT (t), args,
					in_decl, /*entering_scope=*/1);
	  else
	    context = NULL_TREE;

	  /* Then, figure out what arguments are appropriate for the
	     type we are trying to find.  For example, given:

	       template <class T> struct S;
	       template <class T, class U> void f(T, U) { S<U> su; }

	     and supposing that we are instantiating f<int, double>,
	     then our ARGS will be {int, double}, but, when looking up
	     S we only want {double}.  */
	  argvec = tsubst (TYPE_TI_ARGS (t), args, in_decl);

  	  r = lookup_template_class (t, argvec, in_decl, context,
				     entering_scope);

	  return cp_build_type_variant (r, TYPE_READONLY (t),
					TYPE_VOLATILE (t));
	}
      else 
	/* This is not a template type, so there's nothing to do.  */
	return t;

    default:
      return tsubst (t, args, in_decl);
    }
}

/* Substitute the ARGS into the T, which is a _DECL.  TYPE is the
   (already computed) substitution of ARGS into TREE_TYPE (T), if
   appropriate.  Return the result of the substitution.  IN_DECL is as
   for tsubst.  */

tree
tsubst_decl (t, args, type, in_decl)
     tree t;
     tree args;
     tree type;
     tree in_decl;
{
  int saved_lineno;
  char* saved_filename;
  tree r;

  /* Set the filename and linenumber to improve error-reporting.  */
  saved_lineno = lineno;
  saved_filename = input_filename;
  lineno = DECL_SOURCE_LINE (t);
  input_filename = DECL_SOURCE_FILE (t);

  switch (TREE_CODE (t))
    {
    case TEMPLATE_DECL:
      {
	/* We can get here when processing a member template function
	   of a template class.  */
	tree decl = DECL_TEMPLATE_RESULT (t);
	tree parms;
	tree* new_parms;
	tree spec;
	int is_template_template_parm = DECL_TEMPLATE_TEMPLATE_PARM_P (t);

	if (!is_template_template_parm)
	  {
	    /* We might already have an instance of this template.
	       The ARGS are for the surrounding class type, so the
	       full args contain the tsubst'd args for the context,
	       plus the innermost args from the template decl.  */
	    tree tmpl_args = DECL_CLASS_TEMPLATE_P (t) 
	      ? CLASSTYPE_TI_ARGS (TREE_TYPE (t))
	      : DECL_TI_ARGS (DECL_RESULT (t));
	    tree full_args = tsubst (tmpl_args, args, in_decl);

	    /* tsubst_template_arg_vector doesn't copy the vector if
	       nothing changed.  But, *something* should have
	       changed.  */
	    my_friendly_assert (full_args != tmpl_args, 0);

	    spec = retrieve_specialization (t, full_args);
	    if (spec != NULL_TREE)
	      {
		r = spec;
		break;
	      }
	  }

	/* Make a new template decl.  It will be similar to the
	   original, but will record the current template arguments. 
	   We also create a new function declaration, which is just
	   like the old one, but points to this new template, rather
	   than the old one.  */
	r = copy_node (t);
	copy_lang_decl (r);
	my_friendly_assert (DECL_LANG_SPECIFIC (r) != 0, 0);
	TREE_CHAIN (r) = NULL_TREE;

	if (is_template_template_parm)
	  {
	    tree new_decl = tsubst (decl, args, in_decl);
	    DECL_RESULT (r) = new_decl;
	    TREE_TYPE (r) = TREE_TYPE (new_decl);
	    break;
	  }

	DECL_CONTEXT (r) 
	  = tsubst_aggr_type (DECL_CONTEXT (t), args, in_decl,
			      /*entering_scope=*/1);
	DECL_CLASS_CONTEXT (r) 
	  = tsubst_aggr_type (DECL_CLASS_CONTEXT (t), args, in_decl,
			      /*entering_scope=*/1);
	DECL_TEMPLATE_INFO (r) = build_tree_list (t, args);

	if (TREE_CODE (decl) == TYPE_DECL)
	  {
	    tree new_type = tsubst (TREE_TYPE (t), args, in_decl);
	    TREE_TYPE (r) = new_type;
	    CLASSTYPE_TI_TEMPLATE (new_type) = r;
	    DECL_RESULT (r) = TYPE_MAIN_DECL (new_type);
	    DECL_TI_ARGS (r) = CLASSTYPE_TI_ARGS (new_type);
	  }
	else
	  {
	    tree new_decl = tsubst (decl, args, in_decl);
	    DECL_RESULT (r) = new_decl;
	    DECL_TI_TEMPLATE (new_decl) = r;
	    TREE_TYPE (r) = TREE_TYPE (new_decl);
	    DECL_TI_ARGS (r) = DECL_TI_ARGS (new_decl);
	  }

	SET_DECL_IMPLICIT_INSTANTIATION (r);
	DECL_TEMPLATE_INSTANTIATIONS (r) = NULL_TREE;
	DECL_TEMPLATE_SPECIALIZATIONS (r) = NULL_TREE;

	/* The template parameters for this new template are all the
	   template parameters for the old template, except the
	   outermost level of parameters. */
	DECL_TEMPLATE_PARMS (r) 
	  = tsubst_template_parms (DECL_TEMPLATE_PARMS (t), args);

	if (PRIMARY_TEMPLATE_P (t))
	  DECL_PRIMARY_TEMPLATE (r) = r;

	/* We don't partially instantiate partial specializations.  */
	if (TREE_CODE (decl) == TYPE_DECL)
	  break;

	for (spec = DECL_TEMPLATE_SPECIALIZATIONS (t);
	     spec != NULL_TREE;
	     spec = TREE_CHAIN (spec))
	  {
	    /* It helps to consider example here.  Consider:

	       template <class T>
	       struct S {
	         template <class U>
		 void f(U u);

		 template <>
		 void f(T* t) {}
	       };
	       
	       Now, for example, we are instantiating S<int>::f(U u).  
	       We want to make a template:

	       template <class U>
	       void S<int>::f(U);

	       It will have a specialization, for the case U = int*, of
	       the form:

	       template <>
	       void S<int>::f<int*>(int*);

	       This specialization will be an instantiation of
	       the specialization given in the declaration of S, with
	       argument list int*.  */

	    tree fn = TREE_VALUE (spec);
	    tree spec_args;
	    tree new_fn;

	    if (!DECL_TEMPLATE_SPECIALIZATION (fn))
	      /* Instantiations are on the same list, but they're of
		 no concern to us.  */
	      continue;

	    if (TREE_CODE (fn) != TEMPLATE_DECL)
	      /* A full specialization.  There's no need to record
		 that here.  */
	      continue;

	    spec_args = tsubst (DECL_TI_ARGS (fn), args, in_decl); 
	    new_fn = tsubst (DECL_RESULT (most_general_template (fn)), 
			     spec_args, in_decl); 
	    DECL_TI_TEMPLATE (new_fn) = fn;
	    register_specialization (new_fn, r, 
				     innermost_args (spec_args));
	  }

	/* Record this partial instantiation.  */
	register_specialization (r, t, 
				 DECL_TI_ARGS (DECL_RESULT (r)));

      }
      break;

    case FUNCTION_DECL:
      {
	tree ctx;
	tree argvec;
	tree gen_tmpl;
	int member;

	/* Nobody should be tsubst'ing into non-template functions.  */
	my_friendly_assert (DECL_TEMPLATE_INFO (t) != NULL_TREE, 0);

	if (TREE_CODE (DECL_TI_TEMPLATE (t)) == TEMPLATE_DECL)
	  {
	    tree spec;

	    /* Calculate the most general template of which R is a
	       specialization, and the complete set of arguments used to
	       specialize R.  */
	    gen_tmpl = most_general_template (DECL_TI_TEMPLATE (t));
	    argvec = tsubst (DECL_TI_ARGS (t), args, in_decl);

	    /* Check to see if we already have this specialization.  */
	    spec = retrieve_specialization (gen_tmpl, argvec);
	    if (spec)
	      {
		r = spec;
		break;
	      }
	  }
	else
	  {
	    /* This special case arises when we have something like this:

	         template <class T> struct S { 
		   friend void f<int>(int, double); 
		 };

	       Here, the DECL_TI_TEMPLATE for the friend declaration
	       will be a LOOKUP_EXPR or an IDENTIFIER_NODE.  We are
	       being called from tsubst_friend_function, and we want
	       only to create a new decl (R) with appropriate types so
	       that we can call determine_specialization.  */
	    my_friendly_assert ((TREE_CODE (DECL_TI_TEMPLATE (t)) 
				 == LOOKUP_EXPR)
				|| (TREE_CODE (DECL_TI_TEMPLATE (t))
				    == IDENTIFIER_NODE), 0);
	    gen_tmpl = NULL_TREE;
	  }

	if (DECL_CLASS_SCOPE_P (t))
	  {
	    if (DECL_NAME (t) == constructor_name (DECL_CONTEXT (t)))
	      member = 2;
	    else
	      member = 1;
	    ctx = tsubst_aggr_type (DECL_CLASS_CONTEXT (t), args, t,
				    /*entering_scope=*/1);
	  }
	else
	  {
	    member = 0;
	    ctx = NULL_TREE;
	  }
	type = tsubst (type, args, in_decl);

	/* We do NOT check for matching decls pushed separately at this
           point, as they may not represent instantiations of this
           template, and in any case are considered separate under the
           discrete model.  Instead, see add_maybe_template.  */

	r = copy_node (t);
	copy_lang_decl (r);
	DECL_USE_TEMPLATE (r) = 0;
	TREE_TYPE (r) = type;

	DECL_CONTEXT (r)
	  = tsubst_aggr_type (DECL_CONTEXT (t), args, t, /*entering_scope=*/1);
	DECL_CLASS_CONTEXT (r) = ctx;

	if (member && !strncmp (OPERATOR_TYPENAME_FORMAT,
				IDENTIFIER_POINTER (DECL_NAME (r)),
				sizeof (OPERATOR_TYPENAME_FORMAT) - 1))
	  {
	    /* Type-conversion operator.  Reconstruct the name, in
	       case it's the name of one of the template's parameters.  */
	    DECL_NAME (r) = build_typename_overload (TREE_TYPE (type));
	  }

	DECL_ARGUMENTS (r) = tsubst (DECL_ARGUMENTS (t), args, t);
	DECL_MAIN_VARIANT (r) = r;
	DECL_RESULT (r) = NULL_TREE;
	DECL_INITIAL (r) = NULL_TREE;

	TREE_STATIC (r) = 0;
	TREE_PUBLIC (r) = TREE_PUBLIC (t);
	DECL_EXTERNAL (r) = 1;
	DECL_INTERFACE_KNOWN (r) = 0;
	DECL_DEFER_OUTPUT (r) = 0;
	TREE_CHAIN (r) = NULL_TREE;
	DECL_PENDING_INLINE_INFO (r) = 0;
	TREE_USED (r) = 0;

	if (DECL_CONSTRUCTOR_P (r))
	  {
	    maybe_retrofit_in_chrg (r);
	    grok_ctor_properties (ctx, r);
	  }
	if (IDENTIFIER_OPNAME_P (DECL_NAME (r)))
	  grok_op_properties (r, DECL_VIRTUAL_P (r), DECL_FRIEND_P (r));

	/* Set up the DECL_TEMPLATE_INFO for R and compute its mangled
	   name.  There's no need to do this in the special friend
	   case mentioned above where GEN_TMPL is NULL.  */
	if (gen_tmpl)
	  {
	    DECL_TEMPLATE_INFO (r) 
	      = perm_tree_cons (gen_tmpl, argvec, NULL_TREE);
	    SET_DECL_IMPLICIT_INSTANTIATION (r);
	    register_specialization (r, gen_tmpl, argvec);

	    /* Set the mangled name for R.  */
	    if (DECL_DESTRUCTOR_P (t))
	      DECL_ASSEMBLER_NAME (r) = build_destructor_name (ctx);
	    else 
	      {
		/* Instantiations of template functions must be mangled
		   specially, in order to conform to 14.5.5.1
		   [temp.over.link].  */
		tree tmpl = DECL_TI_TEMPLATE (t);
		
		/* TMPL will be NULL if this is a specialization of a
		   member function of a template class.  */
		if (name_mangling_version < 1
		    || tmpl == NULL_TREE
		    || (member && !is_member_template (tmpl)
			&& !DECL_TEMPLATE_INFO (tmpl)))
		  set_mangled_name_for_decl (r);
		else
		  set_mangled_name_for_template_decl (r);
	      }
	    
	    DECL_RTL (r) = 0;
	    make_decl_rtl (r, NULL_PTR, 1);
	    
	    /* Like grokfndecl.  If we don't do this, pushdecl will
	       mess up our TREE_CHAIN because it doesn't find a
	       previous decl.  Sigh.  */
	    if (member
		&& (IDENTIFIER_GLOBAL_VALUE (DECL_ASSEMBLER_NAME (r)) 
		    == NULL_TREE))
	      SET_IDENTIFIER_GLOBAL_VALUE (DECL_ASSEMBLER_NAME (r), r);
	  }
      }
      break;

    case PARM_DECL:
      {
	r = copy_node (t);
	TREE_TYPE (r) = type;
	if (TREE_CODE (DECL_INITIAL (r)) != TEMPLATE_PARM_INDEX)
	  DECL_INITIAL (r) = TREE_TYPE (r);
	else
	  DECL_INITIAL (r) = tsubst (DECL_INITIAL (r), args, in_decl);

	DECL_CONTEXT (r) = NULL_TREE;
#ifdef PROMOTE_PROTOTYPES
	if ((TREE_CODE (type) == INTEGER_TYPE
	     || TREE_CODE (type) == ENUMERAL_TYPE)
	    && TYPE_PRECISION (type) < TYPE_PRECISION (integer_type_node))
	  DECL_ARG_TYPE (r) = integer_type_node;
#endif
	if (TREE_CHAIN (t))
	  TREE_CHAIN (r) = tsubst (TREE_CHAIN (t), args, TREE_CHAIN (t));
      }
      break;

    case FIELD_DECL:
      {
	r = copy_node (t);
	TREE_TYPE (r) = type;
	copy_lang_decl (r);
#if 0
	DECL_FIELD_CONTEXT (r) = tsubst (DECL_FIELD_CONTEXT (t), args, in_decl);
#endif
	DECL_INITIAL (r) = tsubst_expr (DECL_INITIAL (t), args, in_decl);
	TREE_CHAIN (r) = NULL_TREE;
	if (TREE_CODE (type) == VOID_TYPE)
	  cp_error_at ("instantiation of `%D' as type void", r);
      }
      break;

    case USING_DECL:
      {
	r = copy_node (t);
	DECL_INITIAL (r)
	  = tsubst_copy (DECL_INITIAL (t), args, in_decl);
	TREE_CHAIN (r) = NULL_TREE;
      }
      break;

    case VAR_DECL:
      {
	tree argvec;
	tree gen_tmpl;
	tree spec;
	tree tmpl;
	tree ctx = tsubst_aggr_type (DECL_CONTEXT (t), args, in_decl,
				     /*entering_scope=*/1);
	
	/* Nobody should be tsubst'ing into non-template variables.  */
	my_friendly_assert (DECL_LANG_SPECIFIC (t) 
			    && DECL_TEMPLATE_INFO (t) != NULL_TREE, 0);

	/* Check to see if we already have this specialization.  */
	tmpl = DECL_TI_TEMPLATE (t);
	gen_tmpl = most_general_template (tmpl);
	argvec = tsubst (DECL_TI_ARGS (t), args, in_decl);
	spec = retrieve_specialization (gen_tmpl, argvec);
	
	if (spec)
	  {
	    r = spec;
	    break;
	  }

	r = copy_node (t);
	TREE_TYPE (r) = type;
	DECL_CONTEXT (r) = ctx;
	if (TREE_STATIC (r))
	  DECL_ASSEMBLER_NAME (r)
	    = build_static_name (DECL_CONTEXT (r), DECL_NAME (r));

	/* Don't try to expand the initializer until someone tries to use
	   this variable; otherwise we run into circular dependencies.  */
	DECL_INITIAL (r) = NULL_TREE;
	DECL_RTL (r) = 0;
	DECL_SIZE (r) = 0;
	copy_lang_decl (r);
	DECL_CLASS_CONTEXT (r) = DECL_CONTEXT (r);

	DECL_TEMPLATE_INFO (r) = perm_tree_cons (tmpl, argvec, NULL_TREE);
	SET_DECL_IMPLICIT_INSTANTIATION (r);
	register_specialization (r, gen_tmpl, argvec);

	TREE_CHAIN (r) = NULL_TREE;
	if (TREE_CODE (type) == VOID_TYPE)
	  cp_error_at ("instantiation of `%D' as type void", r);
      }
      break;

    case TYPE_DECL:
      if (t == TYPE_NAME (TREE_TYPE (t)))
	r = TYPE_NAME (type);
      else
	{
	  r = copy_node (t);
	  TREE_TYPE (r) = type;
	  DECL_CONTEXT (r) = current_class_type;
	  TREE_CHAIN (r) = NULL_TREE;
	}
      break;

    default:
      my_friendly_abort (0);
    } 

  /* Restore the file and line information.  */
  lineno = saved_lineno;
  input_filename = saved_filename;

  return r;
}


/* Take the tree structure T and replace template parameters used therein
   with the argument vector ARGS.  IN_DECL is an associated decl for
   diagnostics.

   tsubst is used for dealing with types, decls and the like; for
   expressions, use tsubst_expr or tsubst_copy.  */

tree
tsubst (t, args, in_decl)
     tree t, args;
     tree in_decl;
{
  tree type;

  if (t == NULL_TREE || t == error_mark_node
      || t == integer_type_node
      || t == void_type_node
      || t == char_type_node
      || TREE_CODE (t) == NAMESPACE_DECL)
    return t;

  if (TREE_CODE (t) == IDENTIFIER_NODE)
    type = IDENTIFIER_TYPE_VALUE (t);
  else
    type = TREE_TYPE (t);
  if (type == unknown_type_node)
    my_friendly_abort (42);

  if (type && TREE_CODE (t) != FUNCTION_DECL
      && TREE_CODE (t) != TYPENAME_TYPE
      && TREE_CODE (t) != TEMPLATE_DECL
      && TREE_CODE (t) != IDENTIFIER_NODE)
    type = tsubst (type, args, in_decl);

  if (TREE_CODE_CLASS (TREE_CODE (t)) == 'd')
    return tsubst_decl (t, args, type, in_decl);

  switch (TREE_CODE (t))
    {
    case RECORD_TYPE:
    case UNION_TYPE:
    case ENUMERAL_TYPE:
      return tsubst_aggr_type (t, args, in_decl, /*entering_scope=*/0);

    case ERROR_MARK:
    case IDENTIFIER_NODE:
    case OP_IDENTIFIER:
    case VOID_TYPE:
    case REAL_TYPE:
    case COMPLEX_TYPE:
    case BOOLEAN_TYPE:
    case INTEGER_CST:
    case REAL_CST:
    case STRING_CST:
      return t;

    case INTEGER_TYPE:
      if (t == integer_type_node)
	return t;

      if (TREE_CODE (TYPE_MIN_VALUE (t)) == INTEGER_CST
	  && TREE_CODE (TYPE_MAX_VALUE (t)) == INTEGER_CST)
	return t;

      {
	tree max = TREE_OPERAND (TYPE_MAX_VALUE (t), 0);
	max = tsubst_expr (max, args, in_decl);
	if (processing_template_decl)
	  {
	    tree itype = make_node (INTEGER_TYPE);
	    TYPE_MIN_VALUE (itype) = size_zero_node;
	    TYPE_MAX_VALUE (itype) = build_min (MINUS_EXPR, sizetype, max,
						integer_one_node);
	    return itype;
	  }

	max = fold (build_binary_op (MINUS_EXPR, max, integer_one_node, 1));
	return build_index_2_type (size_zero_node, max);
      }

    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case TEMPLATE_PARM_INDEX:
      {
	int idx;
	int level;
	int levels;
	tree r = NULL_TREE;

	if (TREE_CODE (t) == TEMPLATE_TYPE_PARM
	    || TREE_CODE (t) == TEMPLATE_TEMPLATE_PARM)
	  {
	    idx = TEMPLATE_TYPE_IDX (t);
	    level = TEMPLATE_TYPE_LEVEL (t);
	  }
	else
	  {
	    idx = TEMPLATE_PARM_IDX (t);
	    level = TEMPLATE_PARM_LEVEL (t);
	  }

	if (TREE_VEC_LENGTH (args) > 0)
	  {
	    tree arg = NULL_TREE;

	    levels = TMPL_ARGS_DEPTH (args);
	    if (level <= levels)
	      arg = TMPL_ARG (args, level, idx);

	    if (arg != NULL_TREE)
	      {
		if (TREE_CODE (t) == TEMPLATE_TYPE_PARM)
		  {
		    my_friendly_assert (TREE_CODE_CLASS (TREE_CODE (arg))
					== 't', 0);
		    return cp_build_type_variant
		      (arg, TYPE_READONLY (arg) || TYPE_READONLY (t),
		       TYPE_VOLATILE (arg) || TYPE_VOLATILE (t));
		  }
		else if (TREE_CODE (t) == TEMPLATE_TEMPLATE_PARM)
		  {
		    if (CLASSTYPE_TEMPLATE_INFO (t))
		      {
			/* We are processing a type constructed from
			   a template template parameter */
			tree argvec = tsubst (CLASSTYPE_TI_ARGS (t),
					      args, in_decl);
			tree r;

			/* We can get a TEMPLATE_TEMPLATE_PARM here when 
			   we are resolving nested-types in the signature of 
			   a member function templates.
			   Otherwise ARG is a TEMPLATE_DECL and is the real 
			   template to be instantiated.  */
			if (TREE_CODE (arg) == TEMPLATE_TEMPLATE_PARM)
			  arg = TYPE_NAME (arg);

			r = lookup_template_class (DECL_NAME (arg), 
						   argvec, in_decl, 
						   DECL_CONTEXT (arg),
						   /*entering_scope=*/0);
			return cp_build_type_variant (r, TYPE_READONLY (t),
						      TYPE_VOLATILE (t));
		      }
		    else
		      /* We are processing a template argument list.  */ 
		      return arg;
		  }
		else
		  return arg;
	      }
	  }

	if (level == 1)
	  /* This can happen during the attempted tsubst'ing in
	     unify.  This means that we don't yet have any information
	     about the template parameter in question.  */
	  return t;

	/* If we get here, we must have been looking at a parm for a
	   more deeply nested template.  Make a new version of this
	   template parameter, but with a lower level.  */
	switch (TREE_CODE (t))
	  {
	  case TEMPLATE_TYPE_PARM:
	  case TEMPLATE_TEMPLATE_PARM:
	    r = copy_node (t);
	    TEMPLATE_TYPE_PARM_INDEX (r)
	      = reduce_template_parm_level (TEMPLATE_TYPE_PARM_INDEX (t),
					    r, levels);
	    TYPE_STUB_DECL (r) = TYPE_NAME (r) = TEMPLATE_TYPE_DECL (r);
	    TYPE_MAIN_VARIANT (r) = r;
	    TYPE_POINTER_TO (r) = NULL_TREE;
	    TYPE_REFERENCE_TO (r) = NULL_TREE;

	    if (TREE_CODE (t) == TEMPLATE_TEMPLATE_PARM
		&& CLASSTYPE_TEMPLATE_INFO (t))
	      {
		tree argvec = tsubst (CLASSTYPE_TI_ARGS (t), args, in_decl);
		CLASSTYPE_TEMPLATE_INFO (r)
		  = perm_tree_cons (TYPE_NAME (t), argvec, NULL_TREE);
	      }
	    break;

	  case TEMPLATE_PARM_INDEX:
	    r = reduce_template_parm_level (t, type, levels);
	    break;
	   
	  default:
	    my_friendly_abort (0);
	  }

	return r;
      }

    case TREE_LIST:
      {
	tree purpose, value, chain, result;
	int via_public, via_virtual, via_protected;

	if (t == void_list_node)
	  return t;

	via_public = TREE_VIA_PUBLIC (t);
	via_protected = TREE_VIA_PROTECTED (t);
	via_virtual = TREE_VIA_VIRTUAL (t);

	purpose = TREE_PURPOSE (t);
	if (purpose)
	  purpose = tsubst (purpose, args, in_decl);
	value = TREE_VALUE (t);
	if (value)
	  value = tsubst (value, args, in_decl);
	chain = TREE_CHAIN (t);
	if (chain && chain != void_type_node)
	  chain = tsubst (chain, args, in_decl);
	if (purpose == TREE_PURPOSE (t)
	    && value == TREE_VALUE (t)
	    && chain == TREE_CHAIN (t))
	  return t;
	result = hash_tree_cons (via_public, via_virtual, via_protected,
				 purpose, value, chain);
	TREE_PARMLIST (result) = TREE_PARMLIST (t);
	return result;
      }
    case TREE_VEC:
      if (type != NULL_TREE)
	{
	  /* A binfo node.  We always need to make a copy, of the node
	     itself and of its BINFO_BASETYPES.  */

	  t = copy_node (t);

	  /* Make sure type isn't a typedef copy.  */
	  type = BINFO_TYPE (TYPE_BINFO (type));

	  TREE_TYPE (t) = complete_type (type);
	  if (IS_AGGR_TYPE (type))
	    {
	      BINFO_VTABLE (t) = TYPE_BINFO_VTABLE (type);
	      BINFO_VIRTUALS (t) = TYPE_BINFO_VIRTUALS (type);
	      if (TYPE_BINFO_BASETYPES (type) != NULL_TREE)
		BINFO_BASETYPES (t) = copy_node (TYPE_BINFO_BASETYPES (type));
	    }
	  return t;
	}

      /* Otherwise, a vector of template arguments.  */
      return tsubst_template_arg_vector (t, args);

    case POINTER_TYPE:
    case REFERENCE_TYPE:
      {
	tree r;
	enum tree_code code;

	if (type == TREE_TYPE (t))
	  return t;

	code = TREE_CODE (t);
	if (TREE_CODE (type) == REFERENCE_TYPE) 
	  {
	    static int   last_line = 0;
	    static char* last_file = 0;

	    /* We keep track of the last time we issued this error
	       message to avoid spewing a ton of messages during a
	       single bad template instantiation.  */
	    if (last_line != lineno ||
		last_file != input_filename)
	      {
		cp_error ("cannot form type %s to reference type %T during template instantiation",
			  (code == POINTER_TYPE) ? "pointer" : "reference",
			  type);
		last_line = lineno;
		last_file = input_filename;
	      }

	    /* Use the underlying type in an attempt at error
	       recovery; maybe the user meant vector<int> and wrote
	       vector<int&>, or some such.  */
	    if (code == REFERENCE_TYPE)
	      r = type;
	    else
	      r = build_pointer_type (TREE_TYPE (type));
	  }
	else if (code == POINTER_TYPE)
	  r = build_pointer_type (type);
	else
	  r = build_reference_type (type);
	r = cp_build_type_variant (r, TYPE_READONLY (t), TYPE_VOLATILE (t));

	/* Will this ever be needed for TYPE_..._TO values?  */
	layout_type (r);
	return r;
      }
    case OFFSET_TYPE:
      return build_offset_type
	(tsubst (TYPE_OFFSET_BASETYPE (t), args, in_decl), type);
    case FUNCTION_TYPE:
    case METHOD_TYPE:
      {
	tree values = TYPE_ARG_TYPES (t);
	tree context = TYPE_CONTEXT (t);
	tree raises = TYPE_RAISES_EXCEPTIONS (t);
	tree fntype;

	/* Don't bother recursing if we know it won't change anything.	*/
	if (values != void_list_node)
	  {
	    /* This should probably be rewritten to use hash_tree_cons for
               the memory savings.  */
	    tree first = NULL_TREE;
	    tree last = NULL_TREE;

	    for (; values && values != void_list_node;
		 values = TREE_CHAIN (values))
	      {
		tree value = TYPE_MAIN_VARIANT (type_decays_to
		  (tsubst (TREE_VALUE (values), args, in_decl)));
		/* Don't instantiate default args unless they are used.
		   Handle it in build_over_call instead.  */
		tree purpose = TREE_PURPOSE (values);
		tree x = build_tree_list (purpose, value);

		if (first)
		  TREE_CHAIN (last) = x;
		else
		  first = x;
		last = x;
	      }

	    if (values == void_list_node)
	      TREE_CHAIN (last) = void_list_node;

	    values = first;
	  }
	if (context)
	  context = tsubst (context, args, in_decl);
	/* Could also optimize cases where return value and
	   values have common elements (e.g., T min(const &T, const T&).  */

	/* If the above parameters haven't changed, just return the type.  */
	if (type == TREE_TYPE (t)
	    && values == TYPE_VALUES (t)
	    && context == TYPE_CONTEXT (t))
	  return t;

	/* Construct a new type node and return it.  */
	if (TREE_CODE (t) == FUNCTION_TYPE
	    && context == NULL_TREE)
	  {
	    fntype = build_function_type (type, values);
	  }
	else if (context == NULL_TREE)
	  {
	    tree base = tsubst (TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (t))),
				args, in_decl);
	    fntype = build_cplus_method_type (base, type,
					      TREE_CHAIN (values));
	  }
	else
	  {
	    fntype = make_node (TREE_CODE (t));
	    TREE_TYPE (fntype) = type;
	    TYPE_CONTEXT (fntype) = FROB_CONTEXT (context);
	    TYPE_VALUES (fntype) = values;
	    TYPE_SIZE (fntype) = TYPE_SIZE (t);
	    TYPE_ALIGN (fntype) = TYPE_ALIGN (t);
	    TYPE_MODE (fntype) = TYPE_MODE (t);
	    if (TYPE_METHOD_BASETYPE (t))
	      TYPE_METHOD_BASETYPE (fntype) = tsubst (TYPE_METHOD_BASETYPE (t),
						      args, in_decl);
	    /* Need to generate hash value.  */
	    my_friendly_abort (84);
	  }
	fntype = build_type_variant (fntype,
				     TYPE_READONLY (t),
				     TYPE_VOLATILE (t));
	if (raises)
	  {
	    raises = tsubst (raises, args, in_decl);
	    fntype = build_exception_variant (fntype, raises);
	  }
	return fntype;
      }
    case ARRAY_TYPE:
      {
	tree domain = tsubst (TYPE_DOMAIN (t), args, in_decl);
	tree r;
	if (type == TREE_TYPE (t) && domain == TYPE_DOMAIN (t))
	  return t;
	r = build_cplus_array_type (type, domain);
	return r;
      }

    case PLUS_EXPR:
    case MINUS_EXPR:
      return fold (build (TREE_CODE (t), TREE_TYPE (t),
			  tsubst (TREE_OPERAND (t, 0), args, in_decl),
			  tsubst (TREE_OPERAND (t, 1), args, in_decl)));

    case NEGATE_EXPR:
    case NOP_EXPR:
      return fold (build1 (TREE_CODE (t), TREE_TYPE (t),
			   tsubst (TREE_OPERAND (t, 0), args, in_decl)));

    case TYPENAME_TYPE:
      {
	tree ctx = tsubst_aggr_type (TYPE_CONTEXT (t), args, in_decl,
				     /*entering_scope=*/1);
	tree f = tsubst_copy (TYPENAME_TYPE_FULLNAME (t), args, in_decl);

	/* Normally, make_typename_type does not require that the CTX
	   have complete type in order to allow things like:
	     
             template <class T> struct S { typename S<T>::X Y; };

	   But, such constructs have already been resolved by this
	   point, so here CTX really should have complete type, unless
	   it's a partial instantiation.  */
	if (!uses_template_parms (ctx) 
	    && !complete_type_or_else (ctx))
	  return error_mark_node;

	f = make_typename_type (ctx, f);
	return cp_build_type_variant
	  (f, TYPE_READONLY (f) || TYPE_READONLY (t),
	   TYPE_VOLATILE (f) || TYPE_VOLATILE (t));
      }

    case INDIRECT_REF:
      return make_pointer_declarator
	(type, tsubst (TREE_OPERAND (t, 0), args, in_decl));
      
    case ADDR_EXPR:
      return make_reference_declarator
	(type, tsubst (TREE_OPERAND (t, 0), args, in_decl));

    case ARRAY_REF:
      return build_parse_node
	(ARRAY_REF, tsubst (TREE_OPERAND (t, 0), args, in_decl),
	 tsubst_expr (TREE_OPERAND (t, 1), args, in_decl));

    case CALL_EXPR:
      return make_call_declarator
	(tsubst (TREE_OPERAND (t, 0), args, in_decl),
	 tsubst (TREE_OPERAND (t, 1), args, in_decl),
	 TREE_OPERAND (t, 2),
	 tsubst (TREE_TYPE (t), args, in_decl));

    case SCOPE_REF:
      return build_parse_node
	(TREE_CODE (t), tsubst (TREE_OPERAND (t, 0), args, in_decl),
	 tsubst (TREE_OPERAND (t, 1), args, in_decl));

    default:
      sorry ("use of `%s' in template",
	     tree_code_name [(int) TREE_CODE (t)]);
      return error_mark_node;
    }
}

void
do_pushlevel ()
{
  emit_line_note (input_filename, lineno);
  pushlevel (0);
  clear_last_expr ();
  push_momentary ();
  expand_start_bindings (0);
}  

tree
do_poplevel ()
{
  tree t;
  int saved_warn_unused = 0;

  if (processing_template_decl)
    {
      saved_warn_unused = warn_unused;
      warn_unused = 0;
    }
  expand_end_bindings (getdecls (), kept_level_p (), 0);
  if (processing_template_decl)
    warn_unused = saved_warn_unused;
  t = poplevel (kept_level_p (), 1, 0);
  pop_momentary ();
  return t;
}

/* Like tsubst, but deals with expressions.  This function just replaces
   template parms; to finish processing the resultant expression, use
   tsubst_expr.  */

tree
tsubst_copy (t, args, in_decl)
     tree t, args;
     tree in_decl;
{
  enum tree_code code;

  if (t == NULL_TREE || t == error_mark_node)
    return t;

  code = TREE_CODE (t);

  switch (code)
    {
    case PARM_DECL:
      return do_identifier (DECL_NAME (t), 0, NULL_TREE);

    case CONST_DECL:
      {
	tree enum_type;
	tree v;

	if (!DECL_CONTEXT (t))
	  /* This is a global enumeration constant.  */
	  return t;

	/* Unfortunately, we cannot just call lookup_name here.
	 Consider:

	 template <int I> int f() {
	   enum E { a = I };
	   struct S { void g() { E e = a; } };
	 };

	 When we instantiate f<7>::S::g(), say, lookup_name is not
	 clever enough to find f<7>::a.  */
	enum_type 
	  = tsubst_aggr_type (TREE_TYPE (t), args, in_decl, 
			      /*entering_scope=*/0);

	for (v = TYPE_VALUES (enum_type); 
	     v != NULL_TREE; 
	     v = TREE_CHAIN (v))
	  if (TREE_PURPOSE (v) == DECL_NAME (t))
	    return TREE_VALUE (v);

	  /* We didn't find the name.  That should never happen; if
	     name-lookup found it during preliminary parsing, we
	     should find it again here during instantiation.  */
	my_friendly_abort (0);
      }
      break;

    case FIELD_DECL:
      if (DECL_CONTEXT (t))
	{
	  tree ctx;

	  ctx = tsubst_aggr_type (DECL_CONTEXT (t), args, in_decl,
				  /*entering_scope=*/1);
	  if (ctx != DECL_CONTEXT (t))
	    return lookup_field (ctx, DECL_NAME (t), 0, 0);
	}
      return t;

    case VAR_DECL:
    case FUNCTION_DECL:
      if (DECL_LANG_SPECIFIC (t) && DECL_TEMPLATE_INFO (t))
	t = tsubst (t, args, in_decl);
      mark_used (t);
      return t;

    case TEMPLATE_DECL:
      if (is_member_template (t))
	return tsubst (t, args, in_decl);
      else
	return t;

    case LOOKUP_EXPR:
      {
	/* We must tsbust into a LOOKUP_EXPR in case the names to
	   which it refers is a conversion operator; in that case the
	   name will change.  We avoid making unnecessary copies,
	   however.  */
	
	tree id = tsubst_copy (TREE_OPERAND (t, 0), args, in_decl);

	if (id != TREE_OPERAND (t, 0))
	  {
	    tree r = build_nt (LOOKUP_EXPR, id);
	    LOOKUP_EXPR_GLOBAL (r) = LOOKUP_EXPR_GLOBAL (t);
	    t = r;
	  }

	return t;
      }

    case CAST_EXPR:
    case REINTERPRET_CAST_EXPR:
    case CONST_CAST_EXPR:
    case STATIC_CAST_EXPR:
    case DYNAMIC_CAST_EXPR:
      return build1
	(code, tsubst (TREE_TYPE (t), args, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 0), args, in_decl));

    case INDIRECT_REF:
    case PREDECREMENT_EXPR:
    case PREINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case POSTINCREMENT_EXPR:
    case NEGATE_EXPR:
    case TRUTH_NOT_EXPR:
    case BIT_NOT_EXPR:
    case ADDR_EXPR:
    case CONVERT_EXPR:      /* Unary + */
    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
    case ARROW_EXPR:
    case THROW_EXPR:
    case TYPEID_EXPR:
      return build1
	(code, NULL_TREE,
	 tsubst_copy (TREE_OPERAND (t, 0), args, in_decl));

    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case EXACT_DIV_EXPR:
    case BIT_AND_EXPR:
    case BIT_ANDTC_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case TRUNC_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case RSHIFT_EXPR:
    case LSHIFT_EXPR:
    case RROTATE_EXPR:
    case LROTATE_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case MAX_EXPR:
    case MIN_EXPR:
    case LE_EXPR:
    case GE_EXPR:
    case LT_EXPR:
    case GT_EXPR:
    case COMPONENT_REF:
    case ARRAY_REF:
    case COMPOUND_EXPR:
    case SCOPE_REF:
    case DOTSTAR_EXPR:
    case MEMBER_REF:
      return build_nt
	(code, tsubst_copy (TREE_OPERAND (t, 0), args, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 1), args, in_decl));

    case CALL_EXPR:
      {
	tree fn = TREE_OPERAND (t, 0);
	if (is_overloaded_fn (fn))
	  fn = tsubst_copy (get_first_fn (fn), args, in_decl);
	else
	  /* Sometimes FN is a LOOKUP_EXPR.  */
	  fn = tsubst_copy (fn, args, in_decl);
	return build_nt
	  (code, fn, tsubst_copy (TREE_OPERAND (t, 1), args, in_decl),
	   NULL_TREE);
      }

    case METHOD_CALL_EXPR:
      {
	tree name = TREE_OPERAND (t, 0);
	if (TREE_CODE (name) == BIT_NOT_EXPR)
	  {
	    name = tsubst_copy (TREE_OPERAND (name, 0), args, in_decl);
	    name = build1 (BIT_NOT_EXPR, NULL_TREE, name);
	  }
	else if (TREE_CODE (name) == SCOPE_REF
		 && TREE_CODE (TREE_OPERAND (name, 1)) == BIT_NOT_EXPR)
	  {
	    tree base = tsubst_copy (TREE_OPERAND (name, 0), args, in_decl);
	    name = TREE_OPERAND (name, 1);
	    name = tsubst_copy (TREE_OPERAND (name, 0), args, in_decl);
	    name = build1 (BIT_NOT_EXPR, NULL_TREE, name);
	    name = build_nt (SCOPE_REF, base, name);
	  }
	else
	  name = tsubst_copy (TREE_OPERAND (t, 0), args, in_decl);
	return build_nt
	  (code, name, tsubst_copy (TREE_OPERAND (t, 1), args, in_decl),
	   tsubst_copy (TREE_OPERAND (t, 2), args, in_decl),
	   NULL_TREE);
      }

    case BIND_EXPR:
    case COND_EXPR:
    case MODOP_EXPR:
      {
	tree r = build_nt
	  (code, tsubst_copy (TREE_OPERAND (t, 0), args, in_decl),
	   tsubst_copy (TREE_OPERAND (t, 1), args, in_decl),
	   tsubst_copy (TREE_OPERAND (t, 2), args, in_decl));

	if (code == BIND_EXPR && !processing_template_decl)
	  {
	    /* This processing  should really occur in tsubst_expr,
	       However, tsubst_expr does not recurse into expressions,
	       since it assumes that there aren't any statements
	       inside them.  Instead, it simply calls
	       build_expr_from_tree.  So, we need to expand the
	       BIND_EXPR here.  */ 
	    tree rtl_expr = begin_stmt_expr ();
	    tree block = tsubst_expr (TREE_OPERAND (r, 1), args, in_decl);
	    r = finish_stmt_expr (rtl_expr, block);
	  }

	return r;
      }

    case NEW_EXPR:
      {
	tree r = build_nt
	(code, tsubst_copy (TREE_OPERAND (t, 0), args, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 1), args, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 2), args, in_decl));
	NEW_EXPR_USE_GLOBAL (r) = NEW_EXPR_USE_GLOBAL (t);
	return r;
      }

    case DELETE_EXPR:
      {
	tree r = build_nt
	(code, tsubst_copy (TREE_OPERAND (t, 0), args, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 1), args, in_decl));
	DELETE_EXPR_USE_GLOBAL (r) = DELETE_EXPR_USE_GLOBAL (t);
	DELETE_EXPR_USE_VEC (r) = DELETE_EXPR_USE_VEC (t);
	return r;
      }

    case TEMPLATE_ID_EXPR:
      {
        /* Substituted template arguments */
	tree targs = tsubst_copy (TREE_OPERAND (t, 1), args, in_decl);
	tree chain;
	for (chain = targs; chain; chain = TREE_CHAIN (chain))
	  TREE_VALUE (chain) = maybe_fold_nontype_arg (TREE_VALUE (chain));

	return lookup_template_function
	  (tsubst_copy (TREE_OPERAND (t, 0), args, in_decl), targs);
      }

    case TREE_LIST:
      {
	tree purpose, value, chain;

	if (t == void_list_node)
	  return t;

	purpose = TREE_PURPOSE (t);
	if (purpose)
	  purpose = tsubst_copy (purpose, args, in_decl);
	value = TREE_VALUE (t);
	if (value)
	  value = tsubst_copy (value, args, in_decl);
	chain = TREE_CHAIN (t);
	if (chain && chain != void_type_node)
	  chain = tsubst_copy (chain, args, in_decl);
	if (purpose == TREE_PURPOSE (t)
	    && value == TREE_VALUE (t)
	    && chain == TREE_CHAIN (t))
	  return t;
	return tree_cons (purpose, value, chain);
      }

    case RECORD_TYPE:
    case UNION_TYPE:
    case ENUMERAL_TYPE:
    case INTEGER_TYPE:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case TEMPLATE_PARM_INDEX:
    case POINTER_TYPE:
    case REFERENCE_TYPE:
    case OFFSET_TYPE:
    case FUNCTION_TYPE:
    case METHOD_TYPE:
    case ARRAY_TYPE:
    case TYPENAME_TYPE:
    case TYPE_DECL:
      return tsubst (t, args, in_decl);

    case IDENTIFIER_NODE:
      if (IDENTIFIER_TYPENAME_P (t))
	return build_typename_overload
	  (tsubst (TREE_TYPE (t), args, in_decl));
      else
	return t;

    case CONSTRUCTOR:
      return build
	(CONSTRUCTOR, tsubst (TREE_TYPE (t), args, in_decl), NULL_TREE,
	 tsubst_copy (CONSTRUCTOR_ELTS (t), args, in_decl));

    default:
      return t;
    }
}

/* Like tsubst_copy, but also does semantic processing and RTL expansion.  */

tree
tsubst_expr (t, args, in_decl)
     tree t, args;
     tree in_decl;
{
  if (t == NULL_TREE || t == error_mark_node)
    return t;

  if (processing_template_decl)
    return tsubst_copy (t, args, in_decl);

  switch (TREE_CODE (t))
    {
    case RETURN_STMT:
      lineno = TREE_COMPLEXITY (t);
      finish_return_stmt (tsubst_expr (RETURN_EXPR (t),
				       args, in_decl));
      break;

    case EXPR_STMT:
      lineno = TREE_COMPLEXITY (t);
      finish_expr_stmt (tsubst_expr (EXPR_STMT_EXPR (t),
				     args, in_decl));
      break;

    case DECL_STMT:
      {
	int i = suspend_momentary ();
	tree dcl, init;

	lineno = TREE_COMPLEXITY (t);
	emit_line_note (input_filename, lineno);
	dcl = start_decl
	  (tsubst (TREE_OPERAND (t, 0), args, in_decl),
	   tsubst (TREE_OPERAND (t, 1), args, in_decl),
	   TREE_OPERAND (t, 2) != 0, NULL_TREE, NULL_TREE);
	init = tsubst_expr (TREE_OPERAND (t, 2), args, in_decl);
	cp_finish_decl
	  (dcl, init, NULL_TREE, 1, /*init ? LOOKUP_ONLYCONVERTING :*/ 0);
	resume_momentary (i);
	return dcl;
      }

    case FOR_STMT:
      {
	tree tmp;
	lineno = TREE_COMPLEXITY (t);

	begin_for_stmt ();
	for (tmp = FOR_INIT_STMT (t); tmp; tmp = TREE_CHAIN (tmp))
	  tsubst_expr (tmp, args, in_decl);
	finish_for_init_stmt (NULL_TREE);
	finish_for_cond (tsubst_expr (FOR_COND (t), args,
				      in_decl),
			 NULL_TREE);
	tmp = tsubst_expr (FOR_EXPR (t), args, in_decl);
	finish_for_expr (tmp, NULL_TREE);
	tsubst_expr (FOR_BODY (t), args, in_decl);
	finish_for_stmt (tmp, NULL_TREE);
      }
      break;

    case WHILE_STMT:
      {
	lineno = TREE_COMPLEXITY (t);
	begin_while_stmt ();
	finish_while_stmt_cond (tsubst_expr (WHILE_COND (t),
					     args, in_decl),
				NULL_TREE);
	tsubst_expr (WHILE_BODY (t), args, in_decl);
	finish_while_stmt (NULL_TREE);
      }
      break;

    case DO_STMT:
      {
	lineno = TREE_COMPLEXITY (t);
	begin_do_stmt ();
	tsubst_expr (DO_BODY (t), args, in_decl);
	finish_do_body (NULL_TREE);
	finish_do_stmt (tsubst_expr (DO_COND (t), args,
				     in_decl),
			NULL_TREE);
      }
      break;

    case IF_STMT:
      {
	tree tmp;

	lineno = TREE_COMPLEXITY (t);
	begin_if_stmt ();
	finish_if_stmt_cond (tsubst_expr (IF_COND (t),
					  args, in_decl),
			     NULL_TREE);

	if (tmp = THEN_CLAUSE (t), tmp)
	  {
	    tsubst_expr (tmp, args, in_decl);
	    finish_then_clause (NULL_TREE);
	  }

	if (tmp = ELSE_CLAUSE (t), tmp)
	  {
	    begin_else_clause ();
	    tsubst_expr (tmp, args, in_decl);
	    finish_else_clause (NULL_TREE);
	  }

	finish_if_stmt ();
      }
      break;

    case COMPOUND_STMT:
      {
	tree substmt;

	lineno = TREE_COMPLEXITY (t);
	begin_compound_stmt (COMPOUND_STMT_NO_SCOPE (t));
	for (substmt = COMPOUND_BODY (t); 
	     substmt != NULL_TREE;
	     substmt = TREE_CHAIN (substmt))
	  tsubst_expr (substmt, args, in_decl);
	return finish_compound_stmt (COMPOUND_STMT_NO_SCOPE (t), 
				     NULL_TREE);
      }
      break;

    case BREAK_STMT:
      lineno = TREE_COMPLEXITY (t);
      finish_break_stmt ();
      break;

    case CONTINUE_STMT:
      lineno = TREE_COMPLEXITY (t);
      finish_continue_stmt ();
      break;

    case SWITCH_STMT:
      {
	tree val, tmp;

	lineno = TREE_COMPLEXITY (t);
	begin_switch_stmt ();
	val = tsubst_expr (SWITCH_COND (t), args, in_decl);
	finish_switch_cond (val);
	
	if (tmp = TREE_OPERAND (t, 1), tmp)
	  tsubst_expr (tmp, args, in_decl);

	finish_switch_stmt (val, NULL_TREE);
      }
      break;

    case CASE_LABEL:
      finish_case_label (tsubst_expr (CASE_LOW (t), args, in_decl),
			 tsubst_expr (CASE_HIGH (t), args, in_decl));
      break;

    case LABEL_DECL:
      t = define_label (DECL_SOURCE_FILE (t), DECL_SOURCE_LINE (t),
			DECL_NAME (t));
      if (t)
	expand_label (t);
      break;

    case GOTO_STMT:
      lineno = TREE_COMPLEXITY (t);
      t = GOTO_DESTINATION (t);
      if (TREE_CODE (t) != IDENTIFIER_NODE)
	/* Computed goto's must be tsubst'd into.  On the other hand,
	   non-computed gotos must not be; the identifier in question
	   will have no binding.  */
	t = tsubst_expr (t, args, in_decl);
      finish_goto_stmt (t);
      break;

    case ASM_STMT:
      lineno = TREE_COMPLEXITY (t);
      finish_asm_stmt (tsubst_expr (ASM_CV_QUAL (t), args, in_decl),
		       tsubst_expr (ASM_STRING (t), args, in_decl),
		       tsubst_expr (ASM_OUTPUTS (t), args, in_decl),
		       tsubst_expr (ASM_INPUTS (t), args, in_decl), 
		       tsubst_expr (ASM_CLOBBERS (t), args, in_decl));
      break;

    case TRY_BLOCK:
      lineno = TREE_COMPLEXITY (t);
      begin_try_block ();
      tsubst_expr (TRY_STMTS (t), args, in_decl);
      finish_try_block (NULL_TREE);
      {
	tree handler = TRY_HANDLERS (t);
	for (; handler; handler = TREE_CHAIN (handler))
	  tsubst_expr (handler, args, in_decl);
      }
      finish_handler_sequence (NULL_TREE);
      break;

    case HANDLER:
      lineno = TREE_COMPLEXITY (t);
      begin_handler ();
      if (HANDLER_PARMS (t))
	{
	  tree d = HANDLER_PARMS (t);
	  expand_start_catch_block
	    (tsubst (TREE_OPERAND (d, 1), args, in_decl),
	     tsubst (TREE_OPERAND (d, 0), args, in_decl));
	}
      else
	expand_start_catch_block (NULL_TREE, NULL_TREE);
      finish_handler_parms (NULL_TREE);
      tsubst_expr (HANDLER_BODY (t), args, in_decl);
      finish_handler (NULL_TREE);
      break;

    case TAG_DEFN:
      lineno = TREE_COMPLEXITY (t);
      t = TREE_TYPE (t);
      if (TREE_CODE (t) == ENUMERAL_TYPE)
	tsubst (t, args, NULL_TREE);
      break;

    default:
      return build_expr_from_tree (tsubst_copy (t, args, in_decl));
    }
  return NULL_TREE;
}

/* Instantiate the indicated variable of function template TMPL with
   the template arguments in TARG_PTR.  */

tree
instantiate_template (tmpl, targ_ptr)
     tree tmpl, targ_ptr;
{
  tree fndecl;
  tree gen_tmpl;
  tree spec;
  int i, len;
  struct obstack *old_fmp_obstack;
  extern struct obstack *function_maybepermanent_obstack;
  tree inner_args;

  if (tmpl == error_mark_node)
    return error_mark_node;

  my_friendly_assert (TREE_CODE (tmpl) == TEMPLATE_DECL, 283);

  /* Check to see if we already have this specialization.  */
  spec = retrieve_specialization (tmpl, targ_ptr);
  if (spec != NULL_TREE)
    return spec;

  if (DECL_TEMPLATE_INFO (tmpl))
    {
      /* The TMPL is a partial instantiation.  To get a full set of
	 arguments we must add the arguments used to perform the
	 partial instantiation.  */
      targ_ptr = add_outermost_template_args (DECL_TI_ARGS (tmpl),
					      targ_ptr);
      gen_tmpl = most_general_template (tmpl);

      /* Check to see if we already have this specialization.  */
      spec = retrieve_specialization (gen_tmpl, targ_ptr);
      if (spec != NULL_TREE)
	return spec;
    }
  else
    gen_tmpl = tmpl;

  push_obstacks (&permanent_obstack, &permanent_obstack);
  old_fmp_obstack = function_maybepermanent_obstack;
  function_maybepermanent_obstack = &permanent_obstack;

  len = DECL_NTPARMS (gen_tmpl);
  inner_args = innermost_args (targ_ptr);
  i = len;
  while (i--)
    {
      tree t = TREE_VEC_ELT (inner_args, i);
      if (TREE_CODE_CLASS (TREE_CODE (t)) == 't')
	{
	  tree nt = target_type (t);
	  if (IS_AGGR_TYPE (nt) && decl_function_context (TYPE_MAIN_DECL (nt)))
	    {
	      cp_error ("type `%T' composed from a local class is not a valid template-argument", t);
	      cp_error ("  trying to instantiate `%D'", gen_tmpl);
	      fndecl = error_mark_node;
	      goto out;
	    }
	}
    }
  targ_ptr = copy_to_permanent (targ_ptr);

  /* substitute template parameters */
  fndecl = tsubst (DECL_RESULT (gen_tmpl), targ_ptr, gen_tmpl);
  /* The DECL_TI_TEMPLATE should always be the immediate parent
     template, not the most general template.  */
  DECL_TI_TEMPLATE (fndecl) = tmpl;

  if (flag_external_templates)
    add_pending_template (fndecl);

 out:
  function_maybepermanent_obstack = old_fmp_obstack;
  pop_obstacks ();

  return fndecl;
}

/* Push the name of the class template into the scope of the instantiation.  */

void
overload_template_name (type)
     tree type;
{
  tree id = DECL_NAME (CLASSTYPE_TI_TEMPLATE (type));
  tree decl;

  if (IDENTIFIER_CLASS_VALUE (id)
      && TREE_TYPE (IDENTIFIER_CLASS_VALUE (id)) == type)
    return;

  decl = build_decl (TYPE_DECL, id, type);
  SET_DECL_ARTIFICIAL (decl);
  pushdecl_class_level (decl);
}

/* Like type_unification but designed specially to handle conversion
   operators.  

   The FN is a TEMPLATE_DECL for a function.  The ARGS are the
   arguments that are being used when calling it.  

   If FN is a conversion operator, RETURN_TYPE is the type desired as
   the result of the conversion operator.

   The EXTRA_FN_ARG, if any, is the type of an additional
   parameter to be added to the beginning of FN's parameter list.  

   The other arguments are as for type_unification.  */

int
fn_type_unification (fn, explicit_targs, targs, args, return_type,
		     strict, extra_fn_arg)
     tree fn, explicit_targs, targs, args, return_type;
     unification_kind_t strict;
     tree extra_fn_arg;
{
  tree parms;

  my_friendly_assert (TREE_CODE (fn) == TEMPLATE_DECL, 0);

  parms = TYPE_ARG_TYPES (TREE_TYPE (fn));

  if (IDENTIFIER_TYPENAME_P (DECL_NAME (fn))) 
    {
      /* This is a template conversion operator.  Use the return types
         as well as the argument types.  */
      parms = scratch_tree_cons (NULL_TREE, 
				 TREE_TYPE (TREE_TYPE (fn)),
				 parms);
      args = scratch_tree_cons (NULL_TREE, return_type, args);
    }

  if (extra_fn_arg != NULL_TREE)
    parms = scratch_tree_cons (NULL_TREE, extra_fn_arg, parms);

  /* We allow incomplete unification without an error message here
     because the standard doesn't seem to explicitly prohibit it.  Our
     callers must be ready to deal with unification failures in any
     event.  */
  return type_unification (DECL_INNERMOST_TEMPLATE_PARMS (fn), 
			   targs,
			   parms,
			   args,
			   explicit_targs,
			   strict, 1);
}


/* Type unification.

   We have a function template signature with one or more references to
   template parameters, and a parameter list we wish to fit to this
   template.  If possible, produce a list of parameters for the template
   which will cause it to fit the supplied parameter list.

   Return zero for success, 2 for an incomplete match that doesn't resolve
   all the types, and 1 for complete failure.  An error message will be
   printed only for an incomplete match.

   TPARMS[NTPARMS] is an array of template parameter types.

   TARGS[NTPARMS] is the array into which the deduced template
   parameter values are placed.  PARMS is the function template's
   signature (using TEMPLATE_PARM_IDX nodes), and ARGS is the argument
   list we're trying to match against it.

   The EXPLICIT_TARGS are explicit template arguments provided via a
   template-id.

   The parameter STRICT is one of:

   DEDUCE_CALL: 
     We are deducing arguments for a function call, as in
     [temp.deduct.call].

   DEDUCE_CONV:
     We are deducing arguments for a conversion function, as in 
     [temp.deduct.conv].

   DEDUCE_EXACT:
     We are deducing arguments when calculating the partial
     ordering between specializations of function or class
     templates, as in [temp.func.order] and [temp.class.order],
     when doing an explicit instantiation as in [temp.explicit],
     when determining an explicit specialization as in
     [temp.expl.spec], or when taking the address of a function
     template, as in [temp.deduct.funcaddr].  */

int
type_unification (tparms, targs, parms, args, explicit_targs,
		  strict, allow_incomplete)
     tree tparms, targs, parms, args, explicit_targs;
     unification_kind_t strict;
     int allow_incomplete;
{
  int* explicit_mask;
  int i;

  for (i = 0; i < TREE_VEC_LENGTH (tparms); i++)
    TREE_VEC_ELT (targs, i) = NULL_TREE;

  if (explicit_targs != NULL_TREE)
    {
      tree arg_vec;
      arg_vec = coerce_template_parms (tparms, explicit_targs, NULL_TREE, 0,
				       0);

      if (arg_vec == error_mark_node)
	return 1;

      explicit_mask = alloca (sizeof (int) * TREE_VEC_LENGTH (targs));
      bzero ((char *) explicit_mask, sizeof(int) * TREE_VEC_LENGTH (targs));

      for (i = 0; 
	   i < TREE_VEC_LENGTH (arg_vec) 
	     && TREE_VEC_ELT (arg_vec, i) != NULL_TREE;  
	   ++i)
	{
	  TREE_VEC_ELT (targs, i) = TREE_VEC_ELT (arg_vec, i);
	  /* Let unify know that this argument was explicit.  */
	  explicit_mask [i] = 1;
	}
    }
  else
    explicit_mask = 0;

  return 
    type_unification_real (tparms, targs, parms, args, 0,
			   strict, allow_incomplete, explicit_mask); 
}

/* Adjust types before performing type deduction, as described in
   [temp.deduct.call] and [temp.deduct.conv].  The rules in these two
   sections are symmetric.  PARM is the type of a function parameter
   or the return type of the conversion function.  ARG is the type of
   the argument passed to the call, or the type of the value
   intialized with the result of the conversion function.  */

void
maybe_adjust_types_for_deduction (strict, parm, arg)
     unification_kind_t strict;
     tree* parm;
     tree* arg;
{
  switch (strict)
    {
    case DEDUCE_CALL:
      break;

    case DEDUCE_CONV:
      {
	/* Swap PARM and ARG throughout the remainder of this
	   function; the handling is precisely symmetric since PARM
	   will initialize ARG rather than vice versa.  */
	tree* temp = parm;
	parm = arg;
	arg = temp;
	break;
      }

    case DEDUCE_EXACT:
      /* There is nothing to do in this case.  */
      return;

    default:
      my_friendly_abort (0);
    }

  if (TREE_CODE (*parm) != REFERENCE_TYPE)
    {
      /* [temp.deduct.call]
	 
	 If P is not a reference type:
	 
	 --If A is an array type, the pointer type produced by the
	 array-to-pointer standard conversion (_conv.array_) is
	 used in place of A for type deduction; otherwise,
	 
	 --If A is a function type, the pointer type produced by
	 the function-to-pointer standard conversion
	 (_conv.func_) is used in place of A for type deduction;
	 otherwise,
	 
	 --If A is a cv-qualified type, the top level
	 cv-qualifiers of A's type are ignored for type
	 deduction.  */
      if (TREE_CODE (*arg) == ARRAY_TYPE)
	*arg = build_pointer_type (TREE_TYPE (*arg));
      else if (TREE_CODE (*arg) == FUNCTION_TYPE
	  || TREE_CODE (*arg) == METHOD_TYPE)
	*arg = build_pointer_type (*arg);
      else
	*arg = TYPE_MAIN_VARIANT (*arg);
    }
  
  /* [temp.deduct.call]
     
     If P is a cv-qualified type, the top level cv-qualifiers
     of P's type are ignored for type deduction.  If P is a
     reference type, the type referred to by P is used for
     type deduction.  */
  *parm = TYPE_MAIN_VARIANT (*parm);
  if (TREE_CODE (*parm) == REFERENCE_TYPE)
    *parm = TREE_TYPE (*parm);
}

/* Like type_unfication.  EXPLICIT_MASK, if non-NULL, is an array of
   integers, with ones in positions corresponding to arguments in
   targs that were provided explicitly, and zeros elsewhere.  

   If SUBR is 1, we're being called recursively (to unify the
   arguments of a function or method parameter of a function
   template).  */

static int
type_unification_real (tparms, targs, parms, args, subr,
		       strict, allow_incomplete, explicit_mask)
     tree tparms, targs, parms, args;
     int subr;
     unification_kind_t strict;
     int allow_incomplete;
     int* explicit_mask;
{
  tree parm, arg;
  int i;
  int ntparms = TREE_VEC_LENGTH (tparms);
  int sub_strict;

  my_friendly_assert (TREE_CODE (tparms) == TREE_VEC, 289);
  my_friendly_assert (parms == NULL_TREE 
		      || TREE_CODE (parms) == TREE_LIST, 290);
  /* ARGS could be NULL (via a call from parse.y to
     build_x_function_call).  */
  if (args)
    my_friendly_assert (TREE_CODE (args) == TREE_LIST, 291);
  my_friendly_assert (ntparms > 0, 292);

  switch (strict)
    {
    case DEDUCE_CALL:
      sub_strict = UNIFY_ALLOW_MORE_CV_QUAL | UNIFY_ALLOW_DERIVED;
      break;
      
    case DEDUCE_CONV:
      sub_strict = UNIFY_ALLOW_LESS_CV_QUAL;
      break;

    case DEDUCE_EXACT:
      sub_strict = UNIFY_ALLOW_NONE;
      break;
      
    default:
      my_friendly_abort (0);
    }

  while (parms
	 && parms != void_list_node
	 && args
	 && args != void_list_node)
    {
      parm = TREE_VALUE (parms);
      parms = TREE_CHAIN (parms);
      arg = TREE_VALUE (args);
      args = TREE_CHAIN (args);

      if (arg == error_mark_node)
	return 1;
      if (arg == unknown_type_node)
	return 1;

      /* Conversions will be performed on a function argument that
	 corresponds with a function parameter that contains only
	 non-deducible template parameters and explicitly specified
	 template parameters.  */
      if (! uses_template_parms (parm))
	{
	  tree type;

	  if (TREE_CODE_CLASS (TREE_CODE (arg)) != 't')
	    type = TREE_TYPE (arg);
	  else
	    {
	      type = arg;
	      arg = NULL_TREE;
	    }

	  if (strict == DEDUCE_EXACT)
	    {
	      if (comptypes (parm, type, 1))
		continue;
	    }
	  else
	    /* It might work; we shouldn't check now, because we might
	       get into infinite recursion.  Overload resolution will
	       handle it.  */
	    continue;

	  return 1;
	}
	
#if 0
      if (TREE_CODE (arg) == VAR_DECL)
	arg = TREE_TYPE (arg);
      else if (TREE_CODE_CLASS (TREE_CODE (arg)) == 'e')
	arg = TREE_TYPE (arg);
#else
      if (TREE_CODE_CLASS (TREE_CODE (arg)) != 't')
	{
	  my_friendly_assert (TREE_TYPE (arg) != NULL_TREE, 293);
	  if (TREE_CODE (arg) == OVERLOAD
	      && TREE_CODE (OVL_FUNCTION (arg)) == TEMPLATE_DECL)
	    {
	      tree targs;
	      tree arg_type;

	      /* Have to back unify here */
	      arg = OVL_FUNCTION (arg);
	      targs = make_scratch_vec (DECL_NTPARMS (arg));
	      arg_type = TREE_TYPE (arg);
	      maybe_adjust_types_for_deduction (strict, &parm, &arg_type);
	      parm = expr_tree_cons (NULL_TREE, parm, NULL_TREE);
	      arg_type = scratch_tree_cons (NULL_TREE, arg_type, NULL_TREE);
	      return 
		type_unification (DECL_INNERMOST_TEMPLATE_PARMS (arg), 
				  targs, arg_type, parm, NULL_TREE,
				  DEDUCE_EXACT, allow_incomplete); 
	    }
	  arg = TREE_TYPE (arg);
	}
#endif
      if (! flag_ansi && arg == TREE_TYPE (null_node))
	{
	  warning ("using type void* for NULL");
	  arg = ptr_type_node;
	}

      if (!subr)
	maybe_adjust_types_for_deduction (strict, &parm, &arg);

      switch (unify (tparms, targs, parm, arg, sub_strict,
		     explicit_mask)) 
	{
	case 0:
	  break;
	case 1:
	  return 1;
	}
    }
  /* Fail if we've reached the end of the parm list, and more args
     are present, and the parm list isn't variadic.  */
  if (args && args != void_list_node && parms == void_list_node)
    return 1;
  /* Fail if parms are left and they don't have default values.	 */
  if (parms
      && parms != void_list_node
      && TREE_PURPOSE (parms) == NULL_TREE)
    return 1;
  if (!subr)
    for (i = 0; i < ntparms; i++)
      if (TREE_VEC_ELT (targs, i) == NULL_TREE)
	{
	  if (!allow_incomplete)
	    error ("incomplete type unification");
	  return 2;
	}
  return 0;
}

/* Returns the level of DECL, which declares a template parameter.  */

int
template_decl_level (decl)
     tree decl;
{
  switch (TREE_CODE (decl))
    {
    case TYPE_DECL:
    case TEMPLATE_DECL:
      return TEMPLATE_TYPE_LEVEL (TREE_TYPE (decl));

    case PARM_DECL:
      return TEMPLATE_PARM_LEVEL (DECL_INITIAL (decl));

    default:
      my_friendly_abort (0);
      return 0;
    }
}

/* Decide whether ARG can be unified with PARM, considering only the
   cv-qualifiers of each type, given STRICT as documented for unify.
   Returns non-zero iff the unification is OK on that basis.*/

int
check_cv_quals_for_unify (strict, arg, parm)
     int strict;
     tree arg;
     tree parm;
{
  return !((!(strict & UNIFY_ALLOW_MORE_CV_QUAL)
	    && (TYPE_READONLY (arg) < TYPE_READONLY (parm)
		|| TYPE_VOLATILE (arg) < TYPE_VOLATILE (parm)))
	   || (!(strict & UNIFY_ALLOW_LESS_CV_QUAL)
	       && (TYPE_READONLY (arg) > TYPE_READONLY (parm)
		   || TYPE_VOLATILE (arg) > TYPE_VOLATILE (parm))));
}

/* Takes parameters as for type_unification.  Returns 0 if the
   type deduction suceeds, 1 otherwise.  The parameter STRICT is a
   bitwise or of the following flags:

     UNIFY_ALLOW_NONE:
       Require an exact match between PARM and ARG.
     UNIFY_ALLOW_MORE_CV_QUAL:
       Allow the deduced ARG to be more cv-qualified than ARG.
     UNIFY_ALLOW_LESS_CV_QUAL:
       Allow the deduced ARG to be less cv-qualified than ARG.
     UNIFY_ALLOW_DERIVED:
       Allow the deduced ARG to be a template base class of ARG,
       or a pointer to a template base class of the type pointed to by
       ARG.  */

int
unify (tparms, targs, parm, arg, strict, explicit_mask)
     tree tparms, targs, parm, arg;
     int strict;
     int* explicit_mask;
{
  int idx;
  tree targ;
  tree tparm;

  /* I don't think this will do the right thing with respect to types.
     But the only case I've seen it in so far has been array bounds, where
     signedness is the only information lost, and I think that will be
     okay.  */
  while (TREE_CODE (parm) == NOP_EXPR)
    parm = TREE_OPERAND (parm, 0);

  if (arg == error_mark_node)
    return 1;
  if (arg == unknown_type_node)
    return 1;
  /* If PARM uses template parameters, then we can't bail out here,
     even in ARG == PARM, since we won't record unifications for the
     template parameters.  We might need them if we're trying to
     figure out which of two things is more specialized.  */
  if (arg == parm && !uses_template_parms (parm))
    return 0;

  /* Immediately reject some pairs that won't unify because of
     cv-qualification mismatches.  */
  if (TREE_CODE (arg) == TREE_CODE (parm)
      && TREE_CODE_CLASS (TREE_CODE (arg)) == 't'
      /* We check the cv-qualifiers when unifying with template type
	 parameters below.  We want to allow ARG `const T' to unify with
	 PARM `T' for example, when computing which of two templates
	 is more specialized, for example.  */
      && TREE_CODE (arg) != TEMPLATE_TYPE_PARM
      && !check_cv_quals_for_unify (strict, arg, parm))
    return 1;

  switch (TREE_CODE (parm))
    {
    case TYPENAME_TYPE:
      /* In a type which contains a nested-name-specifier, template
	 argument values cannot be deduced for template parameters used
	 within the nested-name-specifier.  */
      return 0;

    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
      tparm = TREE_VALUE (TREE_VEC_ELT (tparms, 0));

      if (TEMPLATE_TYPE_LEVEL (parm)
	  != template_decl_level (tparm))
	/* The PARM is not one we're trying to unify.  Just check
	   to see if it matches ARG.  */
	return (TREE_CODE (arg) == TREE_CODE (parm)
		&& comptypes (parm, arg, 1)) ? 0 : 1;
      idx = TEMPLATE_TYPE_IDX (parm);
      targ = TREE_VEC_ELT (targs, idx);
      tparm = TREE_VALUE (TREE_VEC_ELT (tparms, idx));

      /* Check for mixed types and values.  */
      if ((TREE_CODE (parm) == TEMPLATE_TYPE_PARM
	   && TREE_CODE (tparm) != TYPE_DECL)
	  || (TREE_CODE (parm) == TEMPLATE_TEMPLATE_PARM 
	      && TREE_CODE (tparm) != TEMPLATE_DECL))
	return 1;

      if (!strict && targ != NULL_TREE 
	  && explicit_mask && explicit_mask[idx])
	/* An explicit template argument.  Don't even try to match
	   here; the overload resolution code will manage check to
	   see whether the call is legal.  */ 
	return 0;

      if (TREE_CODE (parm) == TEMPLATE_TEMPLATE_PARM)
	{
	  if (CLASSTYPE_TEMPLATE_INFO (parm))
	    {
	      /* We arrive here when PARM does not involve template 
		 specialization.  */

	      /* ARG must be constructed from a template class.  */
	      if (TREE_CODE (arg) != RECORD_TYPE || !CLASSTYPE_TEMPLATE_INFO (arg))
		return 1;

	      {
		tree parmtmpl = CLASSTYPE_TI_TEMPLATE (parm);
		tree parmvec = CLASSTYPE_TI_ARGS (parm);
		tree argvec = CLASSTYPE_TI_ARGS (arg);
		tree argtmplvec
		  = DECL_INNERMOST_TEMPLATE_PARMS (CLASSTYPE_TI_TEMPLATE (arg));
		int i;

		/* The parameter and argument roles have to be switched here 
		   in order to handle default arguments properly.  For example, 
		   template<template <class> class TT> void f(TT<int>) 
		   should be able to accept vector<int> which comes from 
		   template <class T, class Allocator = allocator> 
		   class vector.  */

		if (coerce_template_parms (argtmplvec, parmvec, parmtmpl, 1, 1)
		    == error_mark_node)
		  return 1;
	  
		/* Deduce arguments T, i from TT<T> or TT<i>.  */
		for (i = 0; i < TREE_VEC_LENGTH (parmvec); ++i)
		  {
		    tree t = TREE_VEC_ELT (parmvec, i);
		    if (TREE_CODE (t) != TEMPLATE_TYPE_PARM
			&& TREE_CODE (t) != TEMPLATE_TEMPLATE_PARM
			&& TREE_CODE (t) != TEMPLATE_PARM_INDEX)
		      continue;

		    /* This argument can be deduced.  */

		    if (unify (tparms, targs, t, 
			       TREE_VEC_ELT (argvec, i), 
			       UNIFY_ALLOW_NONE, explicit_mask))
		      return 1;
		  }
	      }
	      arg = CLASSTYPE_TI_TEMPLATE (arg);
	    }
	}
      else
	{
	  /* If PARM is `const T' and ARG is only `int', we don't have
	     a match unless we are allowing additional qualification.
	     If ARG is `const int' and PARM is just `T' that's OK;
	     that binds `const int' to `T'.  */
	  if (!check_cv_quals_for_unify (strict | UNIFY_ALLOW_LESS_CV_QUAL, 
					 arg, parm))
	    return 1;

	  /* Consider the case where ARG is `const volatile int' and
	     PARM is `const T'.  Then, T should be `volatile int'.  */
	  arg = 
	    cp_build_type_variant (arg, 
				   TYPE_READONLY (arg) > TYPE_READONLY (parm),
				   TYPE_VOLATILE (arg) > TYPE_VOLATILE (parm));
	}

      /* Simple cases: Value already set, does match or doesn't.  */
      if (targ != NULL_TREE 
	  && (comptypes (targ, arg, 1)
	      || (explicit_mask && explicit_mask[idx])))
	return 0;
      else if (targ)
	return 1;
      TREE_VEC_ELT (targs, idx) = arg;
      return 0;

    case TEMPLATE_PARM_INDEX:
      tparm = TREE_VALUE (TREE_VEC_ELT (tparms, 0));

      if (TEMPLATE_PARM_LEVEL (parm) 
	  != template_decl_level (tparm))
	/* The PARM is not one we're trying to unify.  Just check
	   to see if it matches ARG.  */
	return (TREE_CODE (arg) == TREE_CODE (parm)
		&& cp_tree_equal (parm, arg) > 0) ? 0 : 1;

      idx = TEMPLATE_PARM_IDX (parm);
      targ = TREE_VEC_ELT (targs, idx);

      if (targ)
	{
	  int i = (cp_tree_equal (targ, arg) > 0);
	  if (i == 1)
	    return 0;
	  else if (i == 0)
	    return 1;
	  else
	    my_friendly_abort (42);
	}

      TREE_VEC_ELT (targs, idx) = copy_to_permanent (arg);
      return 0;

    case POINTER_TYPE:
      {
	int sub_strict;

	if (TREE_CODE (arg) == RECORD_TYPE && TYPE_PTRMEMFUNC_FLAG (arg))
	  return (unify (tparms, targs, parm, 
			 TYPE_PTRMEMFUNC_FN_TYPE (arg), strict,
			 explicit_mask)); 
	
	if (TREE_CODE (arg) != POINTER_TYPE)
	  return 1;
	
	/* [temp.deduct.call]

	   A can be another pointer or pointer to member type that can
	   be converted to the deduced A via a qualification
	   conversion (_conv.qual_).

	   We pass down STRICT here rather than UNIFY_ALLOW_NONE.
	   This will allow for additional cv-qualification of the
	   pointed-to types if appropriate.  In general, this is a bit
	   too generous; we are only supposed to allow qualification
	   conversions and this method will allow an ARG of char** and
	   a deduced ARG of const char**.  However, overload
	   resolution will subsequently invalidate the candidate, so
	   this is probably OK.  */
	sub_strict = strict;
	
	if (TREE_CODE (TREE_TYPE (arg)) != RECORD_TYPE
	    || TYPE_PTRMEMFUNC_FLAG (TREE_TYPE (arg)))
	  /* The derived-to-base conversion only persists through one
	     level of pointers.  */
	  sub_strict &= ~UNIFY_ALLOW_DERIVED;

	return unify (tparms, targs, TREE_TYPE (parm), TREE_TYPE
		      (arg), sub_strict,  explicit_mask);
      }

    case REFERENCE_TYPE:
      if (TREE_CODE (arg) != REFERENCE_TYPE)
	return 1;
      return unify (tparms, targs, TREE_TYPE (parm), TREE_TYPE (arg),
		    UNIFY_ALLOW_NONE, explicit_mask);

    case ARRAY_TYPE:
      if (TREE_CODE (arg) != ARRAY_TYPE)
	return 1;
      if ((TYPE_DOMAIN (parm) == NULL_TREE)
	  != (TYPE_DOMAIN (arg) == NULL_TREE))
	return 1;
      if (TYPE_DOMAIN (parm) != NULL_TREE
	  && unify (tparms, targs, TYPE_DOMAIN (parm),
		    TYPE_DOMAIN (arg), UNIFY_ALLOW_NONE, explicit_mask) != 0)
	return 1;
      return unify (tparms, targs, TREE_TYPE (parm), TREE_TYPE (arg),
		    UNIFY_ALLOW_NONE, explicit_mask);

    case REAL_TYPE:
    case COMPLEX_TYPE:
    case INTEGER_TYPE:
    case BOOLEAN_TYPE:
    case VOID_TYPE:
      if (TREE_CODE (arg) != TREE_CODE (parm))
	return 1;

      if (TREE_CODE (parm) == INTEGER_TYPE)
	{
	  if (TYPE_MIN_VALUE (parm) && TYPE_MIN_VALUE (arg)
	      && unify (tparms, targs, TYPE_MIN_VALUE (parm),
			TYPE_MIN_VALUE (arg), UNIFY_ALLOW_NONE, explicit_mask))
	    return 1;
	  if (TYPE_MAX_VALUE (parm) && TYPE_MAX_VALUE (arg)
	      && unify (tparms, targs, TYPE_MAX_VALUE (parm),
			TYPE_MAX_VALUE (arg), UNIFY_ALLOW_NONE, explicit_mask))
	    return 1;
	}
      else if (TREE_CODE (parm) == REAL_TYPE
	       /* We use the TYPE_MAIN_VARIANT since we have already
		  checked cv-qualification at the top of the
		  function.  */
	       && !comptypes (TYPE_MAIN_VARIANT (arg),
			      TYPE_MAIN_VARIANT (parm), 1))
	return 1;

      /* As far as unification is concerned, this wins.	 Later checks
	 will invalidate it if necessary.  */
      return 0;

      /* Types INTEGER_CST and MINUS_EXPR can come from array bounds.  */
      /* Type INTEGER_CST can come from ordinary constant template args.  */
    case INTEGER_CST:
      while (TREE_CODE (arg) == NOP_EXPR)
	arg = TREE_OPERAND (arg, 0);

      if (TREE_CODE (arg) != INTEGER_CST)
	return 1;
      return !tree_int_cst_equal (parm, arg);

    case TREE_VEC:
      {
	int i;
	if (TREE_CODE (arg) != TREE_VEC)
	  return 1;
	if (TREE_VEC_LENGTH (parm) != TREE_VEC_LENGTH (arg))
	  return 1;
	for (i = TREE_VEC_LENGTH (parm) - 1; i >= 0; i--)
	  if (unify (tparms, targs,
		     TREE_VEC_ELT (parm, i), TREE_VEC_ELT (arg, i),
		     UNIFY_ALLOW_NONE, explicit_mask))
	    return 1;
	return 0;
      }

    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_FLAG (parm))
	return unify (tparms, targs, TYPE_PTRMEMFUNC_FN_TYPE (parm),
		      arg, strict, explicit_mask);

      if (TREE_CODE (arg) != RECORD_TYPE)
	return 1;
  
      if (CLASSTYPE_TEMPLATE_INFO (parm) && uses_template_parms (parm))
	{
	  tree t = NULL_TREE;
	  if (strict & UNIFY_ALLOW_DERIVED)
	    /* [temp.deduct.call]

	       If P is a class, and P has the form template-id, then A
	       can be a derived class of the deduced A.  Likewise, if
	       P is a pointer to a class of the form template-id, A
	       can be a pointer to a derived class pointed to by the
	       deduced A.  */
	    t = get_template_base (CLASSTYPE_TI_TEMPLATE (parm), arg);
	  else if
	    (CLASSTYPE_TEMPLATE_INFO (arg)
	     && CLASSTYPE_TI_TEMPLATE (parm) == CLASSTYPE_TI_TEMPLATE (arg))
	    t = arg;
	  if (! t || t == error_mark_node)
	    return 1;

	  return unify (tparms, targs, CLASSTYPE_TI_ARGS (parm),
			CLASSTYPE_TI_ARGS (t), UNIFY_ALLOW_NONE,
			explicit_mask);
	}
      else if (!comptypes (TYPE_MAIN_VARIANT (parm),
			   TYPE_MAIN_VARIANT (arg), 1))
	return 1;
      return 0;

    case METHOD_TYPE:
    case FUNCTION_TYPE:
      if (TREE_CODE (arg) != TREE_CODE (parm))
	return 1;

      if (unify (tparms, targs, TREE_TYPE (parm),
		 TREE_TYPE (arg), UNIFY_ALLOW_NONE, explicit_mask))
	return 1;
      return type_unification_real (tparms, targs, TYPE_ARG_TYPES (parm),
				    TYPE_ARG_TYPES (arg), 1, 
				    DEDUCE_EXACT, 0, explicit_mask);

    case OFFSET_TYPE:
      if (TREE_CODE (arg) != OFFSET_TYPE)
	return 1;
      if (unify (tparms, targs, TYPE_OFFSET_BASETYPE (parm),
		 TYPE_OFFSET_BASETYPE (arg), UNIFY_ALLOW_NONE, explicit_mask))
	return 1;
      return unify (tparms, targs, TREE_TYPE (parm), TREE_TYPE (arg),
		    UNIFY_ALLOW_NONE, explicit_mask);

    case CONST_DECL:
      if (arg != decl_constant_value (parm)) 
	return 1;
      return 0;

    case TEMPLATE_DECL:
      /* Matched cases are handled by the ARG == PARM test above.  */
      return 1;

    case MINUS_EXPR:
      if (TREE_CODE (TREE_OPERAND (parm, 1)) == INTEGER_CST)
	{
	  /* We handle this case specially, since it comes up with
	     arrays.  In particular, something like:

	     template <int N> void f(int (&x)[N]);

	     Here, we are trying to unify the range type, which
	     looks like [0 ... (N - 1)].  */
	  tree t, t1, t2;
	  t1 = TREE_OPERAND (parm, 0);
	  t2 = TREE_OPERAND (parm, 1);

	  /* Should this be a regular fold?  */
	  t = maybe_fold_nontype_arg (build (PLUS_EXPR,
					     integer_type_node,
					     arg, t2));

	  return unify (tparms, targs, t1, t, UNIFY_ALLOW_NONE,
			explicit_mask);
	}
      /* else fall through */

    default:
      if (IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (TREE_CODE (parm))))
	{
	  /* We're looking at an expression.  This can happen with
	     something like:

	       template <int I>
	       void foo(S<I>, S<I + 2>);

             If the call looked like:

               foo(S<2>(), S<4>());

	     we would have already matched `I' with `2'.  Now, we'd
	     like to know if `4' matches `I + 2'.  So, we substitute
	     into that expression, and fold constants, in the hope of
	     figuring it out.  */
	  tree t = 
	    maybe_fold_nontype_arg (tsubst_expr (parm, targs, NULL_TREE)); 
	  tree a = maybe_fold_nontype_arg (arg);

	  if (!IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (TREE_CODE (t))))
	    /* Good, we mangaged to simplify the exression.  */
	    return unify (tparms, targs, t, a, UNIFY_ALLOW_NONE,
			  explicit_mask);
	  else
	    /* Bad, we couldn't simplify this.  Assume it doesn't
	       unify.  */
	    return 1;
	}
      else
	sorry ("use of `%s' in template type unification",
	       tree_code_name [(int) TREE_CODE (parm)]);

      return 1;
    }
}

void
mark_decl_instantiated (result, extern_p)
     tree result;
     int extern_p;
{
  if (DECL_TEMPLATE_INSTANTIATION (result))
    SET_DECL_EXPLICIT_INSTANTIATION (result);

  if (TREE_CODE (result) != FUNCTION_DECL)
    /* The TREE_PUBLIC flag for function declarations will have been
       set correctly by tsubst.  */
    TREE_PUBLIC (result) = 1;

  if (! extern_p)
    {
      DECL_INTERFACE_KNOWN (result) = 1;
      DECL_NOT_REALLY_EXTERN (result) = 1;

      /* For WIN32 we also want to put explicit instantiations in
	 linkonce sections.  */
      if (TREE_PUBLIC (result))
	maybe_make_one_only (result);
    }
  else if (TREE_CODE (result) == FUNCTION_DECL)
    mark_inline_for_output (result);
}

/* Given two function templates PAT1 and PAT2, and explicit template
   arguments EXPLICIT_ARGS return:

   1 if PAT1 is more specialized than PAT2 as described in [temp.func.order].
   -1 if PAT2 is more specialized than PAT1.
   0 if neither is more specialized.  */
   
int
more_specialized (pat1, pat2, explicit_args)
     tree pat1, pat2, explicit_args;
{
  tree targs;
  int winner = 0;

  targs = get_bindings_overload (pat1, pat2, explicit_args);
  if (targs)
    --winner;

  targs = get_bindings_overload (pat2, pat1, explicit_args);
  if (targs)
    ++winner;

  return winner;
}

/* Given two class template specialization list nodes PAT1 and PAT2, return:

   1 if PAT1 is more specialized than PAT2 as described in [temp.class.order].
   -1 if PAT2 is more specialized than PAT1.
   0 if neither is more specialized.  */
   
int
more_specialized_class (pat1, pat2)
     tree pat1, pat2;
{
  tree targs;
  int winner = 0;

  targs = get_class_bindings (TREE_VALUE (pat1), TREE_PURPOSE (pat1),
			      TREE_PURPOSE (pat2));
  if (targs)
    --winner;

  targs = get_class_bindings (TREE_VALUE (pat2), TREE_PURPOSE (pat2),
			      TREE_PURPOSE (pat1));
  if (targs)
    ++winner;

  return winner;
}

/* Return the template arguments that will produce the function signature
   DECL from the function template FN, with the explicit template
   arguments EXPLICIT_ARGS.  If CHECK_RETTYPE is 1, the return type must
   also match.  */

static tree
get_bindings_real (fn, decl, explicit_args, check_rettype)
     tree fn, decl, explicit_args;
     int check_rettype;
{
  int ntparms = DECL_NTPARMS (fn);
  tree targs = make_scratch_vec (ntparms);
  tree decl_arg_types = TYPE_ARG_TYPES (TREE_TYPE (decl));
  tree extra_fn_arg = NULL_TREE;
  int i;

  if (DECL_STATIC_FUNCTION_P (fn) 
      && DECL_NONSTATIC_MEMBER_FUNCTION_P (decl))
    {
      /* Sometimes we are trying to figure out what's being
	 specialized by a declaration that looks like a method, and it
	 turns out to be a static member function.  */
      if (CLASSTYPE_TEMPLATE_INFO (DECL_REAL_CONTEXT (fn))
	  && !is_member_template (fn))
	/* The natural thing to do here seems to be to remove the
	   spurious `this' parameter from the DECL, but that prevents
	   unification from making use of the class type.  So,
	   instead, we have fn_type_unification add to the parameters
	   for FN.  */
	extra_fn_arg = build_pointer_type (DECL_REAL_CONTEXT (fn));
      else
	/* In this case, though, adding the extra_fn_arg can confuse
	   things, so we remove from decl_arg_types instead.  */
	decl_arg_types = TREE_CHAIN (decl_arg_types);
    }

  i = fn_type_unification (fn, explicit_args, targs, 
			   decl_arg_types,
			   TREE_TYPE (TREE_TYPE (decl)),
			   DEDUCE_EXACT,
			   extra_fn_arg);

  if (i != 0)
    return NULL_TREE;

  if (check_rettype)
    {
      /* Check to see that the resulting return type is also OK.  */
      tree t = tsubst (TREE_TYPE (TREE_TYPE (fn)), targs,
		       NULL_TREE);

      if (!comptypes (t, TREE_TYPE (TREE_TYPE (decl)), 1))
	return NULL_TREE;
    }

  return targs;
}

/* For most uses, we want to check the return type.  */

tree 
get_bindings (fn, decl, explicit_args)
     tree fn, decl, explicit_args;
{
  return get_bindings_real (fn, decl, explicit_args, 1);
}

/* But for more_specialized, we only care about the parameter types.  */

static tree
get_bindings_overload (fn, decl, explicit_args)
     tree fn, decl, explicit_args;
{
  return get_bindings_real (fn, decl, explicit_args, 0);
}

/* Return the innermost template arguments that, when applied to a
   template specialization whose innermost template parameters are
   TPARMS, and whose specialization arguments are ARGS, yield the
   ARGS.  

   For example, suppose we have:

     template <class T, class U> struct S {};
     template <class T> struct S<T*, int> {};

   Then, suppose we want to get `S<double*, int>'.  The TPARMS will be
   {T}, the PARMS will be {T*, int} and the ARGS will be {double*,
   int}.  The resulting vector will be {double}, indicating that `T'
   is bound to `double'.  */

static tree
get_class_bindings (tparms, parms, args)
     tree tparms, parms, args;
{
  int i, ntparms = TREE_VEC_LENGTH (tparms);
  tree vec = make_temp_vec (ntparms);

  args = innermost_args (args);

  for (i = 0; i < TREE_VEC_LENGTH (parms); ++i)
    {
      switch (unify (tparms, vec, 
		     TREE_VEC_ELT (parms, i), TREE_VEC_ELT (args, i),
		     UNIFY_ALLOW_NONE, 0))
	{
	case 0:
	  break;
	case 1:
	  return NULL_TREE;
	}
    }

  for (i =  0; i < ntparms; ++i)
    if (! TREE_VEC_ELT (vec, i))
      return NULL_TREE;

  return vec;
}

/* Return the most specialized of the list of templates in FNS that can
   produce an instantiation matching DECL, given the explicit template
   arguments EXPLICIT_ARGS.  */

tree
most_specialized (fns, decl, explicit_args)
     tree fns, decl, explicit_args;
{
  tree candidates = NULL_TREE;
  tree fn, champ, args;
  int fate;

  for (fn = fns; fn; fn = TREE_CHAIN (fn))
    {
      tree candidate = TREE_VALUE (fn);

      args = get_bindings (candidate, decl, explicit_args);
      if (args)
	candidates = scratch_tree_cons (NULL_TREE, candidate, 
					candidates);
    }

  if (!candidates)
    return NULL_TREE;

  champ = TREE_VALUE (candidates);
  for (fn = TREE_CHAIN (candidates); fn; fn = TREE_CHAIN (fn))
    {
      fate = more_specialized (champ, TREE_VALUE (fn), explicit_args);
      if (fate == 1)
	;
      else
	{
	  if (fate == 0)
	    {
	      fn = TREE_CHAIN (fn);
	      if (! fn)
		return error_mark_node;
	    }
	  champ = TREE_VALUE (fn);
	}
    }

  for (fn = candidates; fn && TREE_VALUE (fn) != champ; fn = TREE_CHAIN (fn))
    {
      fate = more_specialized (champ, TREE_VALUE (fn), explicit_args);
      if (fate != 1)
	return error_mark_node;
    }

  return champ;
}

/* If DECL is a specialization of some template, return the most
   general such template.  For example, given:

     template <class T> struct S { template <class U> void f(U); };

   if TMPL is `template <class U> void S<int>::f(U)' this will return
   the full template.  This function will not trace past partial
   specializations, however.  For example, given in addition:

     template <class T> struct S<T*> { template <class U> void f(U); };

   if TMPL is `template <class U> void S<int*>::f(U)' this will return
   `template <class T> template <class U> S<T*>::f(U)'.  */

tree
most_general_template (decl)
     tree decl;
{
  while (DECL_TEMPLATE_INFO (decl))
    decl = DECL_TI_TEMPLATE (decl);

  return decl;
}

/* Return the most specialized of the class template specializations
   of TMPL which can produce an instantiation matching ARGS, or
   error_mark_node if the choice is ambiguous.  */

tree
most_specialized_class (tmpl, args)
     tree tmpl;
     tree args;
{
  tree list = NULL_TREE;
  tree t;
  tree champ;
  int fate;

  tmpl = most_general_template (tmpl);
  for (t = DECL_TEMPLATE_SPECIALIZATIONS (tmpl); t; t = TREE_CHAIN (t))
    {
      tree spec_args 
	= get_class_bindings (TREE_VALUE (t), TREE_PURPOSE (t), args);
      if (spec_args)
	{
	  list = decl_tree_cons (TREE_PURPOSE (t), TREE_VALUE (t), list);
	  TREE_TYPE (list) = TREE_TYPE (t);
	}
    }

  if (! list)
    return NULL_TREE;

  t = list;
  champ = t;
  t = TREE_CHAIN (t);
  for (; t; t = TREE_CHAIN (t))
    {
      fate = more_specialized_class (champ, t);
      if (fate == 1)
	;
      else
	{
	  if (fate == 0)
	    {
	      t = TREE_CHAIN (t);
	      if (! t)
		return error_mark_node;
	    }
	  champ = t;
	}
    }

  for (t = list; t && t != champ; t = TREE_CHAIN (t))
    {
      fate = more_specialized_class (champ, t);
      if (fate != 1)
	return error_mark_node;
    }

  return champ;
}

/* called from the parser.  */

void
do_decl_instantiation (declspecs, declarator, storage)
     tree declspecs, declarator, storage;
{
  tree decl = grokdeclarator (declarator, declspecs, NORMAL, 0, NULL_TREE);
  tree result = NULL_TREE;
  int extern_p = 0;

  if (! DECL_LANG_SPECIFIC (decl))
    {
      cp_error ("explicit instantiation of non-template `%#D'", decl);
      return;
    }

  /* If we've already seen this template instance, use it.  */
  if (TREE_CODE (decl) == VAR_DECL)
    {
      result = lookup_field (DECL_CONTEXT (decl), DECL_NAME (decl), 0, 0);
      if (result && TREE_CODE (result) != VAR_DECL)
	result = NULL_TREE;
    }
  else if (TREE_CODE (decl) != FUNCTION_DECL)
    {
      cp_error ("explicit instantiation of `%#D'", decl);
      return;
    }
  else if (DECL_TEMPLATE_SPECIALIZATION (decl))
    /* [temp.spec]

       No program shall both explicit instantiation and explicit
       specialize a template.  */
    {
      cp_error ("explicit instantiation of `%#D' after", decl);
      cp_error_at ("explicit specialization here", decl);
      return;
    }
  else if (DECL_TEMPLATE_INSTANTIATION (decl))
    result = decl;

  if (! result)
    {
      cp_error ("no matching template for `%D' found", decl);
      return;
    }

  if (! DECL_TEMPLATE_INFO (result))
    {
      cp_pedwarn ("explicit instantiation of non-template `%#D'", result);
      return;
    }

  if (flag_external_templates)
    return;

  if (storage == NULL_TREE)
    ;
  else if (storage == ridpointers[(int) RID_EXTERN])
    extern_p = 1;
  else
    cp_error ("storage class `%D' applied to template instantiation",
	      storage);

  mark_decl_instantiated (result, extern_p);
  repo_template_instantiated (result, extern_p);
  if (! extern_p)
    instantiate_decl (result);
}

void
mark_class_instantiated (t, extern_p)
     tree t;
     int extern_p;
{
  SET_CLASSTYPE_EXPLICIT_INSTANTIATION (t);
  SET_CLASSTYPE_INTERFACE_KNOWN (t);
  CLASSTYPE_INTERFACE_ONLY (t) = extern_p;
  CLASSTYPE_VTABLE_NEEDS_WRITING (t) = ! extern_p;
  TYPE_DECL_SUPPRESS_DEBUG (TYPE_NAME (t)) = extern_p;
  if (! extern_p)
    {
      CLASSTYPE_DEBUG_REQUESTED (t) = 1;
      rest_of_type_compilation (t, 1);
    }
}     

void
do_type_instantiation (t, storage)
     tree t, storage;
{
  int extern_p = 0;
  int nomem_p = 0;
  int static_p = 0;

  if (TREE_CODE (t) == TYPE_DECL)
    t = TREE_TYPE (t);

  if (! IS_AGGR_TYPE (t) || ! CLASSTYPE_TEMPLATE_INFO (t))
    {
      cp_error ("explicit instantiation of non-template type `%T'", t);
      return;
    }

  complete_type (t);

  /* With -fexternal-templates, explicit instantiations are treated the same
     as implicit ones.  */
  if (flag_external_templates)
    return;

  if (TYPE_SIZE (t) == NULL_TREE)
    {
      cp_error ("explicit instantiation of `%#T' before definition of template",
		t);
      return;
    }

  if (storage == NULL_TREE)
    /* OK */;
  else if (storage == ridpointers[(int) RID_INLINE])
    nomem_p = 1;
  else if (storage == ridpointers[(int) RID_EXTERN])
    extern_p = 1;
  else if (storage == ridpointers[(int) RID_STATIC])
    static_p = 1;
  else
    {
      cp_error ("storage class `%D' applied to template instantiation",
		storage);
      extern_p = 0;
    }

  /* We've already instantiated this.  */
  if (CLASSTYPE_EXPLICIT_INSTANTIATION (t) && ! CLASSTYPE_INTERFACE_ONLY (t)
      && extern_p)
    return;

  if (! CLASSTYPE_TEMPLATE_SPECIALIZATION (t))
    {
      mark_class_instantiated (t, extern_p);
      repo_template_instantiated (t, extern_p);
    }

  if (nomem_p)
    return;

  {
    tree tmp;

    if (! static_p)
      for (tmp = TYPE_METHODS (t); tmp; tmp = TREE_CHAIN (tmp))
	if (TREE_CODE (tmp) == FUNCTION_DECL
	    && DECL_TEMPLATE_INSTANTIATION (tmp))
	  {
	    mark_decl_instantiated (tmp, extern_p);
	    repo_template_instantiated (tmp, extern_p);
	    if (! extern_p)
	      instantiate_decl (tmp);
	  }

    for (tmp = TYPE_FIELDS (t); tmp; tmp = TREE_CHAIN (tmp))
      if (TREE_CODE (tmp) == VAR_DECL && DECL_TEMPLATE_INSTANTIATION (tmp))
	{
	  mark_decl_instantiated (tmp, extern_p);
	  repo_template_instantiated (tmp, extern_p);
	  if (! extern_p)
	    instantiate_decl (tmp);
	}

    /* In contrast to implicit instantiation, where only the
       declarations, and not the definitions, of members are
       instantiated, we have here:

         [temp.explicit]

	 The explicit instantiation of a class template specialization
	 implies the instantiation of all of its members not
	 previously explicitly specialized in the translation unit
	 containing the explicit instantiation.  

       Of course, we can't instantiate member template classes, since
       we don't have any arguments for them.  */
    for (tmp = CLASSTYPE_TAGS (t); tmp; tmp = TREE_CHAIN (tmp))
      if (IS_AGGR_TYPE (TREE_VALUE (tmp))
	  && !uses_template_parms (CLASSTYPE_TI_ARGS (TREE_VALUE (tmp))))
	do_type_instantiation (TYPE_MAIN_DECL (TREE_VALUE (tmp)), storage);
  }
}

/* Given a function DECL, which is a specialization of TMPL, modify
   DECL to be a re-instantiation of TMPL with the same template
   arguments.  TMPL should be the template into which tsubst'ing
   should occur for DECL, not the most general template.

   One reason for doing this is a scenario like this:

     template <class T>
     void f(const T&, int i);

     void g() { f(3, 7); }

     template <class T>
     void f(const T& t, const int i) { }

   Note that when the template is first instantiated, with
   instantiate_template, the resulting DECL will have no name for the
   first parameter, and the wrong type for the second.  So, when we go
   to instantiate the DECL, we regenerate it.  */

void
regenerate_decl_from_template (decl, tmpl)
     tree decl;
     tree tmpl;
{
  tree args;
  tree save_ti;
  tree code_pattern;
  tree new_decl;
  tree gen_tmpl;
  int unregistered;

  args = DECL_TI_ARGS (decl);
  code_pattern = DECL_TEMPLATE_RESULT (tmpl);

  /* Unregister the specialization so that when we tsubst we will not
     just return DECL.  We don't have to unregister DECL from TMPL
     because if would only be registered there if it were a partial
     instantiation of a specialization, which it isn't: it's a full
     instantiation.  */
  gen_tmpl = most_general_template (tmpl);
  unregistered = unregister_specialization (decl, gen_tmpl);

  /* If the DECL was not unregistered then something peculiar is
     happening: we created a specialization but did not call
     register_specialization for it.  */
  my_friendly_assert (unregistered, 0);

  /* Do the substitution to get the new declaration.  */
  new_decl = tsubst (code_pattern, args, NULL_TREE);

  if (TREE_CODE (decl) == VAR_DECL)
    {
      /* Set up DECL_INITIAL, since tsubst doesn't.  */
      pushclass (DECL_CONTEXT (decl), 2);
      DECL_INITIAL (new_decl) = 
	tsubst_expr (DECL_INITIAL (code_pattern), args, 
		     DECL_TI_TEMPLATE (decl));
      popclass (1);
    }

  if (TREE_CODE (decl) == FUNCTION_DECL)
    /* Convince duplicate_decls to use the DECL_ARGUMENTS from the
       new decl.  */ 
    DECL_INITIAL (new_decl) = error_mark_node;

  /* The immediate parent of the new template is still whatever it was
     before, even though tsubst sets DECL_TI_TEMPLATE up as the most
     general template.  We also reset the DECL_ASSEMBLER_NAME since
     tsubst always calculates the name as if the function in question
     were really a template instance, and sometimes, with friend
     functions, this is not so.  See tsubst_friend_function for
     details.  */
  DECL_TI_TEMPLATE (new_decl) = DECL_TI_TEMPLATE (decl);
  DECL_ASSEMBLER_NAME (new_decl) = DECL_ASSEMBLER_NAME (decl);
  DECL_RTL (new_decl) = DECL_RTL (decl);

  /* Call duplicate decls to merge the old and new declarations.  */
  duplicate_decls (new_decl, decl);

  if (TREE_CODE (decl) == FUNCTION_DECL)
    DECL_INITIAL (new_decl) = NULL_TREE;

  /* Now, re-register the specialization.  */
  register_specialization (decl, gen_tmpl, args);
}

/* Produce the definition of D, a _DECL generated from a template.  */

tree
instantiate_decl (d)
     tree d;
{
  tree tmpl = DECL_TI_TEMPLATE (d);
  tree args = DECL_TI_ARGS (d);
  tree td;
  tree code_pattern;
  tree spec;
  tree gen_tmpl;
  int nested = in_function_p ();
  int pattern_defined;
  int line = lineno;
  char *file = input_filename;

  /* This function should only be used to instantiate templates for
     functions and static member variables.  */
  my_friendly_assert (TREE_CODE (d) == FUNCTION_DECL
		      || TREE_CODE (d) == VAR_DECL, 0);

  if ((TREE_CODE (d) == FUNCTION_DECL && DECL_INITIAL (d))
      || (TREE_CODE (d) == VAR_DECL && !DECL_IN_AGGR_P (d)))
    /* D has already been instantiated.  */
    return d;

  /* If we already have a specialization of this declaration, then
     there's no reason to instantiate it.  Note that
     retrieve_specialization gives us both instantiations and
     specializations, so we must explicitly check
     DECL_TEMPLATE_SPECIALIZATION.  */
  gen_tmpl = most_general_template (tmpl);
  spec = retrieve_specialization (gen_tmpl, args);
  if (spec != NULL_TREE && DECL_TEMPLATE_SPECIALIZATION (spec))
    return spec;

  /* This needs to happen before any tsubsting.  */
  if (! push_tinst_level (d))
    return d;

  for (td = tmpl; 
       DECL_TEMPLATE_INSTANTIATION (td) 
	 /* This next clause handles friend templates defined inside
	    class templates.  The friend templates are not really
	    instantiations from the point of view of the language, but
	    they are instantiations from the point of view of the
	    compiler.  */
	 || (DECL_TEMPLATE_INFO (td) && !DECL_TEMPLATE_SPECIALIZATION (td)); 
       )
    td = DECL_TI_TEMPLATE (td);

  code_pattern = DECL_TEMPLATE_RESULT (td);

  if (TREE_CODE (d) == FUNCTION_DECL)
    pattern_defined = (DECL_INITIAL (code_pattern) != NULL_TREE);
  else
    pattern_defined = ! DECL_IN_AGGR_P (code_pattern);

  push_to_top_level ();
  lineno = DECL_SOURCE_LINE (d);
  input_filename = DECL_SOURCE_FILE (d);

  if (pattern_defined)
    {
      repo_template_used (d);

      if (flag_external_templates && ! DECL_INTERFACE_KNOWN (d))
	{
	  if (flag_alt_external_templates)
	    {
	      if (interface_unknown)
		warn_if_unknown_interface (d);
	    }
	  else if (DECL_INTERFACE_KNOWN (code_pattern))
	    {
	      DECL_INTERFACE_KNOWN (d) = 1;
	      DECL_NOT_REALLY_EXTERN (d) = ! DECL_EXTERNAL (code_pattern);
	    }
	  else
	    warn_if_unknown_interface (code_pattern);
	}

      if (at_eof)
	import_export_decl (d);
    }

  /* Reject all external templates except inline functions.  */
  if (DECL_INTERFACE_KNOWN (d)
      && ! DECL_NOT_REALLY_EXTERN (d)
      && ! (TREE_CODE (d) == FUNCTION_DECL && DECL_INLINE (d)))
    goto out;

  if (TREE_CODE (d) == VAR_DECL 
      && TREE_READONLY (d)
      && DECL_INITIAL (d) == NULL_TREE
      && DECL_INITIAL (code_pattern) != NULL_TREE)
    /* We need to set up DECL_INITIAL regardless of pattern_defined if
	 the variable is a static const initialized in the class body.  */;
  else if (! pattern_defined
	   || (! (TREE_CODE (d) == FUNCTION_DECL && DECL_INLINE (d) && nested)
	       && ! at_eof))
    {
      /* Defer all templates except inline functions used in another
         function.  */
      lineno = line;
      input_filename = file;

      add_pending_template (d);
      goto out;
    }

  regenerate_decl_from_template (d, td);

  /* We already set the file and line above.  Reset them now in case
     they changed as a result of calling regenerate_decl_from_template.  */
  lineno = DECL_SOURCE_LINE (d);
  input_filename = DECL_SOURCE_FILE (d);

  if (TREE_CODE (d) == VAR_DECL)
    {
      DECL_IN_AGGR_P (d) = 0;
      if (DECL_INTERFACE_KNOWN (d))
	DECL_EXTERNAL (d) = ! DECL_NOT_REALLY_EXTERN (d);
      else
	{
	  DECL_EXTERNAL (d) = 1;
	  DECL_NOT_REALLY_EXTERN (d) = 1;
	}
      cp_finish_decl (d, DECL_INITIAL (d), NULL_TREE, 0, 0);
    }
  else if (TREE_CODE (d) == FUNCTION_DECL)
    {
      tree t = DECL_SAVED_TREE (code_pattern);

      start_function (NULL_TREE, d, NULL_TREE, 1);
      store_parm_decls ();

      if (t && TREE_CODE (t) == RETURN_INIT)
	{
	  store_return_init
	    (TREE_OPERAND (t, 0),
	     tsubst_expr (TREE_OPERAND (t, 1), args, tmpl));
	  t = TREE_CHAIN (t);
	}

      if (t && TREE_CODE (t) == CTOR_INITIALIZER)
	{
	  current_member_init_list
	    = tsubst_expr_values (TREE_OPERAND (t, 0), args);
	  current_base_init_list
	    = tsubst_expr_values (TREE_OPERAND (t, 1), args);
	  t = TREE_CHAIN (t);
	}

      setup_vtbl_ptr ();
      /* Always keep the BLOCK node associated with the outermost
	 pair of curly braces of a function.  These are needed
	 for correct operation of dwarfout.c.  */
      keep_next_level ();

      my_friendly_assert (TREE_CODE (t) == COMPOUND_STMT, 42);
      tsubst_expr (t, args, tmpl);

      finish_function (lineno, 0, nested);
    }

out:
  lineno = line;
  input_filename = file;

  pop_from_top_level ();
  pop_tinst_level ();

  return d;
}

tree
tsubst_chain (t, argvec)
     tree t, argvec;
{
  if (t)
    {
      tree first = tsubst (t, argvec, NULL_TREE);
      tree last = first;

      for (t = TREE_CHAIN (t); t; t = TREE_CHAIN (t))
	{
	  tree x = tsubst (t, argvec, NULL_TREE);
	  TREE_CHAIN (last) = x;
	  last = x;
	}

      return first;
    }
  return NULL_TREE;
}

static tree
tsubst_expr_values (t, argvec)
     tree t, argvec;
{
  tree first = NULL_TREE;
  tree *p = &first;

  for (; t; t = TREE_CHAIN (t))
    {
      tree pur = tsubst_copy (TREE_PURPOSE (t), argvec, NULL_TREE);
      tree val = tsubst_expr (TREE_VALUE (t), argvec, NULL_TREE);
      *p = build_tree_list (pur, val);
      p = &TREE_CHAIN (*p);
    }
  return first;
}

tree last_tree;

void
add_tree (t)
     tree t;
{
  last_tree = TREE_CHAIN (last_tree) = t;
}


void
begin_tree ()
{
  saved_trees = tree_cons (NULL_TREE, last_tree, saved_trees);
  last_tree = NULL_TREE;
}


void 
end_tree ()
{
  my_friendly_assert (saved_trees != NULL_TREE, 0);

  last_tree = TREE_VALUE (saved_trees);
  saved_trees = TREE_CHAIN (saved_trees);
}

/* D is an undefined function declaration in the presence of templates with
   the same name, listed in FNS.  If one of them can produce D as an
   instantiation, remember this so we can instantiate it at EOF if D has
   not been defined by that time.  */

void
add_maybe_template (d, fns)
     tree d, fns;
{
  tree t;

  if (DECL_MAYBE_TEMPLATE (d))
    return;

  t = most_specialized (fns, d, NULL_TREE);
  if (! t)
    return;
  if (t == error_mark_node)
    {
      cp_error ("ambiguous template instantiation for `%D'", d);
      return;
    }

  *maybe_template_tail = perm_tree_cons (t, d, NULL_TREE);
  maybe_template_tail = &TREE_CHAIN (*maybe_template_tail);
  DECL_MAYBE_TEMPLATE (d) = 1;
}

/* Instantiate an enumerated type.  */

static tree
tsubst_enum (tag, args)
     tree tag, args;
{
  extern tree current_local_enum;
  tree prev_local_enum = current_local_enum;

  tree newtag = start_enum (TYPE_IDENTIFIER (tag));
  tree e, values = NULL_TREE;

  for (e = TYPE_VALUES (tag); e; e = TREE_CHAIN (e))
    {
      tree value;
      tree elt;

      value = TREE_VALUE (e);
      if (value)
	{
	  if (TREE_CODE (value) == NOP_EXPR)
	    /* This is the special case where the value is really a
	   TEMPLATE_PARM_INDEX.  See finish_enum.  */
	    value = TREE_OPERAND (value, 0);
	  value = tsubst_expr (value, args, NULL_TREE);
	}

      elt = build_enumerator (TREE_PURPOSE (e), value);
      TREE_CHAIN (elt) = values;
      values = elt;
    }

  finish_enum (newtag, values);

  current_local_enum = prev_local_enum;

  return newtag;
}

/* Set the DECL_ASSEMBLER_NAME for DECL, which is a FUNCTION_DECL that
   is either an instantiation or specialization of a template
   function.  */

static void
set_mangled_name_for_template_decl (decl)
     tree decl;
{
  tree saved_namespace;
  tree context;
  tree fn_type;
  tree ret_type;
  tree parm_types;
  tree tparms;
  tree targs;
  tree tmpl;
  int parm_depth;

  my_friendly_assert (TREE_CODE (decl) == FUNCTION_DECL, 0);
  my_friendly_assert (DECL_TEMPLATE_INFO (decl) != NULL_TREE, 0);

  /* The names of template functions must be mangled so as to indicate
     what template is being specialized with what template arguments.
     For example, each of the following three functions must get
     different mangled names:

       void f(int);                  
       template <> void f<7>(int);
       template <> void f<8>(int);  */

  targs = DECL_TI_ARGS (decl);
  if (uses_template_parms (targs))
    /* This DECL is for a partial instantiation.  There's no need to
       mangle the name of such an entity.  */
    return;

  tmpl = most_general_template (DECL_TI_TEMPLATE (decl));
  tparms = DECL_TEMPLATE_PARMS (tmpl);
  parm_depth = TMPL_PARMS_DEPTH (tparms);

  /* There should be as many levels of arguments as there are levels
     of parameters.  */
  my_friendly_assert (parm_depth == TMPL_ARGS_DEPTH (targs), 0);

  /* We now compute the PARMS and RET_TYPE to give to
     build_decl_overload_real.  The PARMS and RET_TYPE are the
     parameter and return types of the template, after all but the
     innermost template arguments have been substituted, not the
     parameter and return types of the function DECL.  For example,
     given:

       template <class T> T f(T);

     both PARMS and RET_TYPE should be `T' even if DECL is `int f(int)'.  
     A more subtle example is:

       template <class T> struct S { template <class U> void f(T, U); }

     Here, if DECL is `void S<int>::f(int, double)', PARMS should be
     {int, U}.  Thus, the args that we want to subsitute into the
     return and parameter type for the function are those in TARGS,
     with the innermost level omitted.  */
  fn_type = TREE_TYPE (tmpl);
  if (DECL_STATIC_FUNCTION_P (decl))
      context = DECL_CLASS_CONTEXT (decl);

  if (parm_depth == 1)
    /* No substitution is necessary.  */
    ;
  else
    {
      int i;
      tree partial_args;

      /* Replace the innermost level of the TARGS with NULL_TREEs to
	 let tsubst know not to subsitute for those parameters.  */
      partial_args = make_temp_vec (TREE_VEC_LENGTH (targs));
      for (i = 1; i < TMPL_ARGS_DEPTH (targs); ++i)
	SET_TMPL_ARGS_LEVEL (partial_args, i,
			     TMPL_ARGS_LEVEL (targs, i));
      SET_TMPL_ARGS_LEVEL (partial_args,
			   TMPL_ARGS_DEPTH (targs),
			   make_temp_vec (DECL_NTPARMS (tmpl)));

      /* Now, do the (partial) substitution to figure out the
	 appropriate function type.  */
      fn_type = tsubst (fn_type, partial_args, NULL_TREE);
      if (DECL_STATIC_FUNCTION_P (decl))
	context = tsubst (context, partial_args, NULL_TREE);

      /* Substitute into the template parameters to obtain the real
	 innermost set of parameters.  This step is important if the
	 innermost set of template parameters contains value
	 parameters whose types depend on outer template parameters.  */
      TREE_VEC_LENGTH (partial_args)--;
      tparms = tsubst_template_parms (tparms, partial_args);
    }

  /* Now, get the innermost parameters and arguments, and figure out
     the parameter and return types.  */
  tparms = INNERMOST_TEMPLATE_PARMS (tparms);
  targs = innermost_args (targs);
  ret_type = TREE_TYPE (fn_type);
  parm_types = TYPE_ARG_TYPES (fn_type);

  /* For a static member function, we generate a fake `this' pointer,
     for the purposes of mangling.  This indicates of which class the
     function is a member.  Because of:

       [class.static] 

       There shall not be a static and a nonstatic member function
       with the same name and the same parameter types

     we don't have to worry that this will result in a clash with a
     non-static member function.  */
  if (DECL_STATIC_FUNCTION_P (decl))
    parm_types = hash_tree_chain (build_pointer_type (context), parm_types);

  /* There should be the same number of template parameters as
     template arguments.  */
  my_friendly_assert (TREE_VEC_LENGTH (tparms) == TREE_VEC_LENGTH (targs),
		      0);

  /* If the template is in a namespace, we need to put that into the
     mangled name. Unfortunately, build_decl_overload_real does not
     get the decl to mangle, so it relies on the current
     namespace. Therefore, we set that here temporarily. */
  my_friendly_assert (TREE_CODE_CLASS (TREE_CODE (decl)) == 'd', 980702);
  saved_namespace = current_namespace;
  current_namespace = CP_DECL_CONTEXT (decl);  

  /* Actually set the DCL_ASSEMBLER_NAME.  */
  DECL_ASSEMBLER_NAME (decl)
    = build_decl_overload_real (DECL_NAME (decl), parm_types, ret_type,
				tparms, targs, 
				DECL_FUNCTION_MEMBER_P (decl) 
				+ DECL_CONSTRUCTOR_P (decl));

  /* Restore the previously active namespace.  */
  current_namespace = saved_namespace;
}


