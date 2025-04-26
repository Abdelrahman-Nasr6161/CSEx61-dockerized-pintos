#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>  // For int64_t

/* Fixed-point real number representation */
typedef int fixed_t;

/* Number of fractional bits */
#define F 16384  /* 1 << 14 */

/* Convert n to fixed point */
#define FP_INT_TO_FP(n) ((n) * (F))

/* Convert x to integer (rounding toward zero) */
#define FP_TO_INT_ZERO(x) ((x) / (F))

/* Convert x to integer (rounding to nearest) */
#define FP_TO_INT_NEAREST(x) (((x) >= 0) ? (((x) + (F)/2)/(F)) : (((x) - (F)/2)/(F)))

/* Add x and y */
#define FP_ADD(x, y) ((x) + (y))

/* Subtract y from x */
#define FP_SUB(x, y) ((x) - (y))

/* Add x and integer n */
#define FP_ADD_MIX(x, n) ((x) + (n) * (F))

/* Subtract integer n from x */
#define FP_SUB_MIX(x, n) ((x) - (n) * (F))

/* Multiply x by y (avoids overflow by using 64-bit intermediate) */
#define FP_MULT(x, y) (((int64_t)(x) * (int64_t)(y)) / (F))

/* Multiply x by integer n */
#define FP_MULT_MIX(x, n) ((x) * (n))

/* Divide x by y (avoids overflow by using 64-bit intermediate) */
#define FP_DIV(x, y) (((int64_t)(x) * (F)) / (int64_t)(y))

/* Divide x by integer n */
#define FP_DIV_MIX(x, n) ((x) / (n))

#endif /* FIXED_POINT_H */