#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

/* Fixed-point real arithmetic implementation.
   We use 17.14 fixed-point representation (17 integer bits, 14 fractional bits). */

typedef int fixed_point_t;

/* Shift amount for fixed-point arithmetic. */
#define F 16384

/* Convert integer to fixed-point. */
#define INT_TO_FP(n) ((n) * (F))

/* Convert fixed-point to integer (rounding toward zero). */
#define FP_TO_INT(x) ((x) / (F))

/* Convert fixed-point to integer (rounding to nearest). */
#define FP_TO_INT_ROUND(x) (((x) >= 0) ? (((x) + (F)/2)/(F)) : (((x) - (F)/2)/(F)))

/* Add two fixed-point numbers. */
#define FP_ADD(x,y) ((x) + (y))

/* Subtract two fixed-point numbers. */
#define FP_SUB(x,y) ((x) - (y))

/* Multiply a fixed-point number by an integer. */
#define FP_MUL_INT(x,n) ((x) * (n))

/* Divide a fixed-point number by an integer. */
#define FP_DIV_INT(x,n) ((x) / (n))

/* Multiply two fixed-point numbers. */
#define FP_MUL(x,y) (((int64_t)(x)) * (y) / (F))

/* Divide two fixed-point numbers. */
#define FP_DIV(x,y) (((int64_t)(x)) * (F) / (y))

#endif /* threads/fixed_point.h */