#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>  // For int64_t

/* Fixed-point real number representation */
typedef int32_t fixed_t;

/* Number of fractional bits (Q14.17 format would use 17, but we'll keep Q14 for compatibility) */
#define F 16384  /* 1 << 14 */

/* Conversion functions */
#define FP_INT_TO_FP(n) ((fixed_t)((n) * (F)))
#define FP_TO_INT_ZERO(x) ((int32_t)((x) / (F)))
#define FP_TO_INT_NEAREST(x) ((int32_t)(((x) >= 0) ? (((x) + (F)/2)/(F)) : (((x) - (F)/2)/(F))))
#define FP_TO_INT_ROUND(x) FP_TO_INT_NEAREST(x)  // Alias for clarity in MLFQS code

/* Arithmetic operations */
#define FP_ADD(x, y) ((fixed_t)((x) + (y)))
#define FP_SUB(x, y) ((fixed_t)((x) - (y)))
#define FP_ADD_MIX(x, n) ((fixed_t)((x) + (n) * (F)))
#define FP_SUB_MIX(x, n) ((fixed_t)((x) - (n) * (F)))

/* Multiplication with overflow protection */
#define FP_MULT(x, y) ((fixed_t)(((int64_t)(x) * (int64_t)(y)) / (F)))
#define FP_MULT_MIX(x, n) ((fixed_t)((x) * (n)))

/* Division with overflow protection */
#define FP_DIV(x, y) ((fixed_t)(((int64_t)(x) * (F)) / (int64_t)(y)))
#define FP_DIV_MIX(x, n) ((fixed_t)((x) / (n)))

/* Additional MLFQS-specific operations */
#define FP_FRAC_PART(x) ((x) & (F - 1))  // Get fractional part
#define FP_INT_PART(x) ((x) / (F))       // Get integer part

#endif /* FIXED_POINT_H */