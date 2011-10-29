/* Prototypes for exported functions defined in avr.c
   
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2006, 2007, 2008, 2009, 2010,
   2011
   Free Software Foundation, Inc.
   Contributed by Denis Chertykov (chertykov@gmail.com)

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */


extern int function_arg_regno_p (int r);
extern void avr_cpu_cpp_builtins (struct cpp_reader * pfile);
extern enum reg_class avr_regno_reg_class (int r);
extern void asm_globalize_label (FILE *file, const char *name);
extern void avr_asm_declare_function_name (FILE *, const char *, tree);
extern void order_regs_for_local_alloc (void);
extern int avr_initial_elimination_offset (int from, int to);
extern int avr_simple_epilogue (void);
extern int avr_hard_regno_rename_ok (unsigned int, unsigned int);
extern rtx avr_return_addr_rtx (int count, rtx tem);
extern bool avr_accumulate_outgoing_args (void);

#ifdef TREE_CODE
extern void avr_asm_output_aligned_decl_common (FILE*, const_tree, const char*, unsigned HOST_WIDE_INT, unsigned int, bool);
extern void asm_output_external (FILE *file, tree decl, char *name);
extern int avr_progmem_p (tree decl, tree attributes);

#ifdef RTX_CODE /* inside TREE_CODE */
extern void init_cumulative_args (CUMULATIVE_ARGS *cum, tree fntype,
				  rtx libname, tree fndecl);
#endif /* RTX_CODE inside TREE_CODE */

#endif /* TREE_CODE */

#ifdef RTX_CODE
extern void asm_output_external_libcall (FILE *file, rtx symref);
extern const char *output_movqi (rtx insn, rtx operands[], int *l);
extern const char *output_movhi (rtx insn, rtx operands[], int *l);
extern const char *out_movqi_r_mr (rtx insn, rtx op[], int *l);
extern const char *out_movqi_mr_r (rtx insn, rtx op[], int *l);
extern const char *out_movhi_r_mr (rtx insn, rtx op[], int *l);
extern const char *out_movhi_mr_r (rtx insn, rtx op[], int *l);
extern const char *out_movsi_r_mr (rtx insn, rtx op[], int *l);
extern const char *out_movsi_mr_r (rtx insn, rtx op[], int *l);
extern const char *output_movsisf (rtx insn, rtx operands[], int *l);
extern const char *avr_out_tstsi (rtx, rtx*, int*);
extern const char *avr_out_tsthi (rtx, rtx*, int*);
extern const char *avr_out_compare (rtx, rtx*, int*);
extern const char *ret_cond_branch (rtx x, int len, int reverse);

extern const char *ashlqi3_out (rtx insn, rtx operands[], int *len);
extern const char *ashlhi3_out (rtx insn, rtx operands[], int *len);
extern const char *ashlsi3_out (rtx insn, rtx operands[], int *len);

extern const char *ashrqi3_out (rtx insn, rtx operands[], int *len);
extern const char *ashrhi3_out (rtx insn, rtx operands[], int *len);
extern const char *ashrsi3_out (rtx insn, rtx operands[], int *len);

extern const char *lshrqi3_out (rtx insn, rtx operands[], int *len);
extern const char *lshrhi3_out (rtx insn, rtx operands[], int *len);
extern const char *lshrsi3_out (rtx insn, rtx operands[], int *len);
extern bool avr_rotate_bytes (rtx operands[]);

extern void expand_prologue (void);
extern void expand_epilogue (bool);
extern int avr_epilogue_uses (int regno);
extern int avr_starting_frame_offset (void);

extern void avr_output_bld (rtx operands[], int bit_nr);
extern void avr_output_addr_vec_elt (FILE *stream, int value);
extern const char *avr_out_sbxx_branch (rtx insn, rtx operands[]);
extern const char* avr_out_bitop (rtx, rtx*, int*);
extern const char* avr_out_plus (rtx*, int*, int*);
extern const char* avr_out_plus_noclobber (rtx*, int*, int*);
extern const char* avr_out_addto_sp (rtx*, int*);
extern bool avr_popcount_each_byte (rtx, int, int);

extern int extra_constraint_Q (rtx x);
extern int adjust_insn_length (rtx insn, int len);
extern const char* output_reload_inhi (rtx*, rtx, int*);
extern const char* output_reload_insisf (rtx*, rtx, int*);
extern void notice_update_cc (rtx body, rtx insn);
extern void print_operand (FILE *file, rtx x, int code);
extern void print_operand_address (FILE *file, rtx addr);
extern int reg_unused_after (rtx insn, rtx reg);
extern int _reg_unused_after (rtx insn, rtx reg);
extern int avr_jump_mode (rtx x, rtx insn);
extern int test_hard_reg_class (enum reg_class rclass, rtx x);
extern int jump_over_one_insn_p (rtx insn, rtx dest);

extern int avr_hard_regno_mode_ok (int regno, enum machine_mode mode);
extern void final_prescan_insn (rtx insn, rtx *operand, int num_operands);
extern int avr_simplify_comparison_p (enum machine_mode mode,
				      RTX_CODE op, rtx x);
extern RTX_CODE avr_normalize_condition (RTX_CODE condition);
extern void out_shift_with_cnt (const char *templ, rtx insn,
				rtx operands[], int *len, int t_len);
extern reg_class_t avr_mode_code_base_reg_class (enum machine_mode, RTX_CODE, RTX_CODE);
extern bool avr_regno_mode_code_ok_for_base_p (int, enum machine_mode, RTX_CODE, RTX_CODE);
extern rtx avr_incoming_return_addr_rtx (void);
extern rtx avr_legitimize_reload_address (rtx*, enum machine_mode, int, int, int, int, rtx (*)(rtx,int));
#endif /* RTX_CODE */

#ifdef REAL_VALUE_TYPE
extern void asm_output_float (FILE *file, REAL_VALUE_TYPE n);
#endif

/* From avr-log.c */

#define avr_edump (avr_log_set_caller_e (__FUNCTION__))
#define avr_fdump (avr_log_set_caller_f (__FUNCTION__))

extern int (*avr_log_set_caller_e (const char*))(const char*, ...);
extern int (*avr_log_set_caller_f (const char*))(FILE*, const char*, ...);

extern void avr_log_set_avr_log (void);

typedef struct
{
  unsigned rtx_costs :1;
  unsigned legitimate_address_p :1;
  unsigned legitimize_address :1;
  unsigned legitimize_reload_address :1;
  unsigned constraints :1;
  unsigned address_cost :1;
} avr_log_t;

extern avr_log_t avr_log;
