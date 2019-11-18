/* 32 physical registers (R0-R31) + SPL + SPH + SREG + PC */
#define FIRST_PSEUDO_REGISTER  36

enum reg_class
{
 NO_REGS,
 GENERAL_REGS,
 ALL_REGS,
 LIM_REG_CLASSES
};

#define N_REG_CLASSES (int) LIM_REG_CLASSES
#define UNITS_PER_WORD 1

#define MOVE_MAX 1
#define CUMULATIVE_ARGS int
