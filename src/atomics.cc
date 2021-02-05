//===-- atomics.cc - Implement OpenMP atomic operations -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of OpenMP atomic operations for the cases
/// when they are not inlined by the compiler.
/// Where possible we just delegate to std::atomic, however for operations on
/// floats, complex, ... which are not supported by C++11's std::atomic we use
/// our own cmpxchg based implementation.
/// We also optimize some operations such as max, min, && || which do not necessarily
/// require an update.

// This description comes from the kmp_atomic.cpp file in teh LLVM OpenMP runtime.
// Here we do not bother to implement some of these operations or data types, since
// their use is rare. (For instance, the float10 ops, and complex<float10> ops).
/*!
@defgroup ATOMIC_OPS Atomic Operations
These functions are used for implementing the many different varieties of atomic
operations.

The compiler is at liberty to inline atomic operations that are naturally
supported by the target architecture. For instance on IA-32 architecture an
atomic like this can be inlined
@code
static int s = 0;
#pragma omp atomic
    s++;
@endcode
using the single instruction: `lock; incl s`

However the runtime does provide entrypoints for these operations to support
compilers that choose not to inline them. (For instance,
`__kmpc_atomic_fixed4_add` could be used to perform the increment above.)
The names of the functions are encoded by using the data type name and the
operation name, as in these tables.

Data Type  | Data type encoding
-----------|---------------
int8_t     | `fixed1`
uint8_t    | `fixed1u`
int16_t    | `fixed2`
uint16_t   | `fixed2u`
int32_t    | `fixed4`
uint32_t   | `fixed4u`
int64_t    | `fixed8`
uint64_t   | `fixed8u`
float      | `float4`
double     | `float8`
float 10 (8087 eighty bit float)  | `float10`
complex<float>   |  `cmplx4`
complex<double>  | `cmplx8`
complex<float10> | `cmplx10`
<br>

Operation | Operation encoding
----------|-------------------
+ | add
- | sub
\* | mul
/ | div
& | andb
<< | shl
\>\> | shr
\| | orb
^  | xor
&& | andl
\|\| | orl
maximum | max
minimum | min
.eqv.   | eqv
.neqv.  | neqv

<br>
For non-commutative operations, `_rev` can also be added for the reversed
operation. For the functions that capture the result, the suffix `_cpt` is
added.

Update Functions
================
The general form of an atomic function that just performs an update (without a
`capture`)
@code
void __kmpc_atomic_<datatype>_<operation>( ident_t *id_ref, int gtid, TYPE *
lhs, TYPE rhs );
@endcode
@param ident_t  a pointer to source location
@param gtid  the global thread id
@param lhs   a pointer to the left operand
@param rhs   the right operand

`capture` functions
===================
The capture functions perform an atomic update and return a result, which is
either the value before the capture, or that after. They take an additional
argument to determine which result is returned.
Their general form is therefore
@code
TYPE __kmpc_atomic_<datatype>_<operation>_cpt( ident_t *id_ref, int gtid, TYPE *
lhs, TYPE rhs, int flag );
@endcode
@param ident_t  a pointer to source location
@param gtid  the global thread id
@param lhs   a pointer to the left operand
@param rhs   the right operand
@param flag  one if the result is to be captured *after* the operation, zero if
captured *before*.

The one set of exceptions to this is the `complex<float>` type where the value
is not returned, rather an extra argument pointer is passed.

They look like
@code
void __kmpc_atomic_cmplx4_<op>_cpt(  ident_t *id_ref, int gtid, kmp_cmplx32 *
lhs, kmp_cmplx32 rhs, kmp_cmplx32 * out, int flag );
@endcode

Read and Write Operations
=========================
The OpenMP<sup>*</sup> standard now supports atomic operations that simply
ensure that the value is read or written atomically, with no modification
performed. In many cases on IA-32 architecture these operations can be inlined
since the architecture guarantees that no tearing occurs on aligned objects
accessed with a single memory operation of up to 64 bits in size.

The general form of the read operations is
@code
TYPE __kmpc_atomic_<type>_rd ( ident_t *id_ref, int gtid, TYPE * loc );
@endcode

For the write operations the form is
@code
void __kmpc_atomic_<type>_wr ( ident_t *id_ref, int gtid, TYPE * lhs, TYPE rhs);
@endcode

Full list of functions
======================
This leads to the generation of 376 atomic functions, as follows.

Functons for integers
---------------------
There are versions here for integers of size 1,2,4 and 8 bytes both signed and
unsigned (where that matters).
@code
    __kmpc_atomic_fixed1_add
    __kmpc_atomic_fixed1_add_cpt
    __kmpc_atomic_fixed1_add_fp
    __kmpc_atomic_fixed1_andb
    __kmpc_atomic_fixed1_andb_cpt
    __kmpc_atomic_fixed1_andl
    __kmpc_atomic_fixed1_andl_cpt
    __kmpc_atomic_fixed1_div
    __kmpc_atomic_fixed1_div_cpt
    __kmpc_atomic_fixed1_div_cpt_rev
    __kmpc_atomic_fixed1_div_float8
    __kmpc_atomic_fixed1_div_fp
    __kmpc_atomic_fixed1_div_rev
    __kmpc_atomic_fixed1_eqv
    __kmpc_atomic_fixed1_eqv_cpt
    __kmpc_atomic_fixed1_max
    __kmpc_atomic_fixed1_max_cpt
    __kmpc_atomic_fixed1_min
    __kmpc_atomic_fixed1_min_cpt
    __kmpc_atomic_fixed1_mul
    __kmpc_atomic_fixed1_mul_cpt
    __kmpc_atomic_fixed1_mul_float8
    __kmpc_atomic_fixed1_mul_fp
    __kmpc_atomic_fixed1_neqv
    __kmpc_atomic_fixed1_neqv_cpt
    __kmpc_atomic_fixed1_orb
    __kmpc_atomic_fixed1_orb_cpt
    __kmpc_atomic_fixed1_orl
    __kmpc_atomic_fixed1_orl_cpt
    __kmpc_atomic_fixed1_rd
    __kmpc_atomic_fixed1_shl
    __kmpc_atomic_fixed1_shl_cpt
    __kmpc_atomic_fixed1_shl_cpt_rev
    __kmpc_atomic_fixed1_shl_rev
    __kmpc_atomic_fixed1_shr
    __kmpc_atomic_fixed1_shr_cpt
    __kmpc_atomic_fixed1_shr_cpt_rev
    __kmpc_atomic_fixed1_shr_rev
    __kmpc_atomic_fixed1_sub
    __kmpc_atomic_fixed1_sub_cpt
    __kmpc_atomic_fixed1_sub_cpt_rev
    __kmpc_atomic_fixed1_sub_fp
    __kmpc_atomic_fixed1_sub_rev
    __kmpc_atomic_fixed1_swp
    __kmpc_atomic_fixed1_wr
    __kmpc_atomic_fixed1_xor
    __kmpc_atomic_fixed1_xor_cpt
    __kmpc_atomic_fixed1u_add_fp
    __kmpc_atomic_fixed1u_sub_fp
    __kmpc_atomic_fixed1u_mul_fp
    __kmpc_atomic_fixed1u_div
    __kmpc_atomic_fixed1u_div_cpt
    __kmpc_atomic_fixed1u_div_cpt_rev
    __kmpc_atomic_fixed1u_div_fp
    __kmpc_atomic_fixed1u_div_rev
    __kmpc_atomic_fixed1u_shr
    __kmpc_atomic_fixed1u_shr_cpt
    __kmpc_atomic_fixed1u_shr_cpt_rev
    __kmpc_atomic_fixed1u_shr_rev
    __kmpc_atomic_fixed2_add
    __kmpc_atomic_fixed2_add_cpt
    __kmpc_atomic_fixed2_add_fp
    __kmpc_atomic_fixed2_andb
    __kmpc_atomic_fixed2_andb_cpt
    __kmpc_atomic_fixed2_andl
    __kmpc_atomic_fixed2_andl_cpt
    __kmpc_atomic_fixed2_div
    __kmpc_atomic_fixed2_div_cpt
    __kmpc_atomic_fixed2_div_cpt_rev
    __kmpc_atomic_fixed2_div_float8
    __kmpc_atomic_fixed2_div_fp
    __kmpc_atomic_fixed2_div_rev
    __kmpc_atomic_fixed2_eqv
    __kmpc_atomic_fixed2_eqv_cpt
    __kmpc_atomic_fixed2_max
    __kmpc_atomic_fixed2_max_cpt
    __kmpc_atomic_fixed2_min
    __kmpc_atomic_fixed2_min_cpt
    __kmpc_atomic_fixed2_mul
    __kmpc_atomic_fixed2_mul_cpt
    __kmpc_atomic_fixed2_mul_float8
    __kmpc_atomic_fixed2_mul_fp
    __kmpc_atomic_fixed2_neqv
    __kmpc_atomic_fixed2_neqv_cpt
    __kmpc_atomic_fixed2_orb
    __kmpc_atomic_fixed2_orb_cpt
    __kmpc_atomic_fixed2_orl
    __kmpc_atomic_fixed2_orl_cpt
    __kmpc_atomic_fixed2_rd
    __kmpc_atomic_fixed2_shl
    __kmpc_atomic_fixed2_shl_cpt
    __kmpc_atomic_fixed2_shl_cpt_rev
    __kmpc_atomic_fixed2_shl_rev
    __kmpc_atomic_fixed2_shr
    __kmpc_atomic_fixed2_shr_cpt
    __kmpc_atomic_fixed2_shr_cpt_rev
    __kmpc_atomic_fixed2_shr_rev
    __kmpc_atomic_fixed2_sub
    __kmpc_atomic_fixed2_sub_cpt
    __kmpc_atomic_fixed2_sub_cpt_rev
    __kmpc_atomic_fixed2_sub_fp
    __kmpc_atomic_fixed2_sub_rev
    __kmpc_atomic_fixed2_swp
    __kmpc_atomic_fixed2_wr
    __kmpc_atomic_fixed2_xor
    __kmpc_atomic_fixed2_xor_cpt
    __kmpc_atomic_fixed2u_add_fp
    __kmpc_atomic_fixed2u_sub_fp
    __kmpc_atomic_fixed2u_mul_fp
    __kmpc_atomic_fixed2u_div
    __kmpc_atomic_fixed2u_div_cpt
    __kmpc_atomic_fixed2u_div_cpt_rev
    __kmpc_atomic_fixed2u_div_fp
    __kmpc_atomic_fixed2u_div_rev
    __kmpc_atomic_fixed2u_shr
    __kmpc_atomic_fixed2u_shr_cpt
    __kmpc_atomic_fixed2u_shr_cpt_rev
    __kmpc_atomic_fixed2u_shr_rev
    __kmpc_atomic_fixed4_add
    __kmpc_atomic_fixed4_add_cpt
    __kmpc_atomic_fixed4_add_fp
    __kmpc_atomic_fixed4_andb
    __kmpc_atomic_fixed4_andb_cpt
    __kmpc_atomic_fixed4_andl
    __kmpc_atomic_fixed4_andl_cpt
    __kmpc_atomic_fixed4_div
    __kmpc_atomic_fixed4_div_cpt
    __kmpc_atomic_fixed4_div_cpt_rev
    __kmpc_atomic_fixed4_div_float8
    __kmpc_atomic_fixed4_div_fp
    __kmpc_atomic_fixed4_div_rev
    __kmpc_atomic_fixed4_eqv
    __kmpc_atomic_fixed4_eqv_cpt
    __kmpc_atomic_fixed4_max
    __kmpc_atomic_fixed4_max_cpt
    __kmpc_atomic_fixed4_min
    __kmpc_atomic_fixed4_min_cpt
    __kmpc_atomic_fixed4_mul
    __kmpc_atomic_fixed4_mul_cpt
    __kmpc_atomic_fixed4_mul_float8
    __kmpc_atomic_fixed4_mul_fp
    __kmpc_atomic_fixed4_neqv
    __kmpc_atomic_fixed4_neqv_cpt
    __kmpc_atomic_fixed4_orb
    __kmpc_atomic_fixed4_orb_cpt
    __kmpc_atomic_fixed4_orl
    __kmpc_atomic_fixed4_orl_cpt
    __kmpc_atomic_fixed4_rd
    __kmpc_atomic_fixed4_shl
    __kmpc_atomic_fixed4_shl_cpt
    __kmpc_atomic_fixed4_shl_cpt_rev
    __kmpc_atomic_fixed4_shl_rev
    __kmpc_atomic_fixed4_shr
    __kmpc_atomic_fixed4_shr_cpt
    __kmpc_atomic_fixed4_shr_cpt_rev
    __kmpc_atomic_fixed4_shr_rev
    __kmpc_atomic_fixed4_sub
    __kmpc_atomic_fixed4_sub_cpt
    __kmpc_atomic_fixed4_sub_cpt_rev
    __kmpc_atomic_fixed4_sub_fp
    __kmpc_atomic_fixed4_sub_rev
    __kmpc_atomic_fixed4_swp
    __kmpc_atomic_fixed4_wr
    __kmpc_atomic_fixed4_xor
    __kmpc_atomic_fixed4_xor_cpt
    __kmpc_atomic_fixed4u_add_fp
    __kmpc_atomic_fixed4u_sub_fp
    __kmpc_atomic_fixed4u_mul_fp
    __kmpc_atomic_fixed4u_div
    __kmpc_atomic_fixed4u_div_cpt
    __kmpc_atomic_fixed4u_div_cpt_rev
    __kmpc_atomic_fixed4u_div_fp
    __kmpc_atomic_fixed4u_div_rev
    __kmpc_atomic_fixed4u_shr
    __kmpc_atomic_fixed4u_shr_cpt
    __kmpc_atomic_fixed4u_shr_cpt_rev
    __kmpc_atomic_fixed4u_shr_rev
    __kmpc_atomic_fixed8_add
    __kmpc_atomic_fixed8_add_cpt
    __kmpc_atomic_fixed8_add_fp
    __kmpc_atomic_fixed8_andb
    __kmpc_atomic_fixed8_andb_cpt
    __kmpc_atomic_fixed8_andl
    __kmpc_atomic_fixed8_andl_cpt
    __kmpc_atomic_fixed8_div
    __kmpc_atomic_fixed8_div_cpt
    __kmpc_atomic_fixed8_div_cpt_rev
    __kmpc_atomic_fixed8_div_float8
    __kmpc_atomic_fixed8_div_fp
    __kmpc_atomic_fixed8_div_rev
    __kmpc_atomic_fixed8_eqv
    __kmpc_atomic_fixed8_eqv_cpt
    __kmpc_atomic_fixed8_max
    __kmpc_atomic_fixed8_max_cpt
    __kmpc_atomic_fixed8_min
    __kmpc_atomic_fixed8_min_cpt
    __kmpc_atomic_fixed8_mul
    __kmpc_atomic_fixed8_mul_cpt
    __kmpc_atomic_fixed8_mul_float8
    __kmpc_atomic_fixed8_mul_fp
    __kmpc_atomic_fixed8_neqv
    __kmpc_atomic_fixed8_neqv_cpt
    __kmpc_atomic_fixed8_orb
    __kmpc_atomic_fixed8_orb_cpt
    __kmpc_atomic_fixed8_orl
    __kmpc_atomic_fixed8_orl_cpt
    __kmpc_atomic_fixed8_rd
    __kmpc_atomic_fixed8_shl
    __kmpc_atomic_fixed8_shl_cpt
    __kmpc_atomic_fixed8_shl_cpt_rev
    __kmpc_atomic_fixed8_shl_rev
    __kmpc_atomic_fixed8_shr
    __kmpc_atomic_fixed8_shr_cpt
    __kmpc_atomic_fixed8_shr_cpt_rev
    __kmpc_atomic_fixed8_shr_rev
    __kmpc_atomic_fixed8_sub
    __kmpc_atomic_fixed8_sub_cpt
    __kmpc_atomic_fixed8_sub_cpt_rev
    __kmpc_atomic_fixed8_sub_fp
    __kmpc_atomic_fixed8_sub_rev
    __kmpc_atomic_fixed8_swp
    __kmpc_atomic_fixed8_wr
    __kmpc_atomic_fixed8_xor
    __kmpc_atomic_fixed8_xor_cpt
    __kmpc_atomic_fixed8u_add_fp
    __kmpc_atomic_fixed8u_sub_fp
    __kmpc_atomic_fixed8u_mul_fp
    __kmpc_atomic_fixed8u_div
    __kmpc_atomic_fixed8u_div_cpt
    __kmpc_atomic_fixed8u_div_cpt_rev
    __kmpc_atomic_fixed8u_div_fp
    __kmpc_atomic_fixed8u_div_rev
    __kmpc_atomic_fixed8u_shr
    __kmpc_atomic_fixed8u_shr_cpt
    __kmpc_atomic_fixed8u_shr_cpt_rev
    __kmpc_atomic_fixed8u_shr_rev
@endcode

Functions for floating point
----------------------------
There are versions here for floating point numbers of size 4, 8, 10 and 16
bytes. (Ten byte floats are used by X87, but are now rare).
@code
    __kmpc_atomic_float4_add
    __kmpc_atomic_float4_add_cpt
    __kmpc_atomic_float4_add_float8
    __kmpc_atomic_float4_add_fp
    __kmpc_atomic_float4_div
    __kmpc_atomic_float4_div_cpt
    __kmpc_atomic_float4_div_cpt_rev
    __kmpc_atomic_float4_div_float8
    __kmpc_atomic_float4_div_fp
    __kmpc_atomic_float4_div_rev
    __kmpc_atomic_float4_max
    __kmpc_atomic_float4_max_cpt
    __kmpc_atomic_float4_min
    __kmpc_atomic_float4_min_cpt
    __kmpc_atomic_float4_mul
    __kmpc_atomic_float4_mul_cpt
    __kmpc_atomic_float4_mul_float8
    __kmpc_atomic_float4_mul_fp
    __kmpc_atomic_float4_rd
    __kmpc_atomic_float4_sub
    __kmpc_atomic_float4_sub_cpt
    __kmpc_atomic_float4_sub_cpt_rev
    __kmpc_atomic_float4_sub_float8
    __kmpc_atomic_float4_sub_fp
    __kmpc_atomic_float4_sub_rev
    __kmpc_atomic_float4_swp
    __kmpc_atomic_float4_wr
    __kmpc_atomic_float8_add
    __kmpc_atomic_float8_add_cpt
    __kmpc_atomic_float8_add_fp
    __kmpc_atomic_float8_div
    __kmpc_atomic_float8_div_cpt
    __kmpc_atomic_float8_div_cpt_rev
    __kmpc_atomic_float8_div_fp
    __kmpc_atomic_float8_div_rev
    __kmpc_atomic_float8_max
    __kmpc_atomic_float8_max_cpt
    __kmpc_atomic_float8_min
    __kmpc_atomic_float8_min_cpt
    __kmpc_atomic_float8_mul
    __kmpc_atomic_float8_mul_cpt
    __kmpc_atomic_float8_mul_fp
    __kmpc_atomic_float8_rd
    __kmpc_atomic_float8_sub
    __kmpc_atomic_float8_sub_cpt
    __kmpc_atomic_float8_sub_cpt_rev
    __kmpc_atomic_float8_sub_fp
    __kmpc_atomic_float8_sub_rev
    __kmpc_atomic_float8_swp
    __kmpc_atomic_float8_wr
    __kmpc_atomic_float10_add
    __kmpc_atomic_float10_add_cpt
    __kmpc_atomic_float10_add_fp
    __kmpc_atomic_float10_div
    __kmpc_atomic_float10_div_cpt
    __kmpc_atomic_float10_div_cpt_rev
    __kmpc_atomic_float10_div_fp
    __kmpc_atomic_float10_div_rev
    __kmpc_atomic_float10_mul
    __kmpc_atomic_float10_mul_cpt
    __kmpc_atomic_float10_mul_fp
    __kmpc_atomic_float10_rd
    __kmpc_atomic_float10_sub
    __kmpc_atomic_float10_sub_cpt
    __kmpc_atomic_float10_sub_cpt_rev
    __kmpc_atomic_float10_sub_fp
    __kmpc_atomic_float10_sub_rev
    __kmpc_atomic_float10_swp
    __kmpc_atomic_float10_wr
    __kmpc_atomic_float16_add
    __kmpc_atomic_float16_add_cpt
    __kmpc_atomic_float16_div
    __kmpc_atomic_float16_div_cpt
    __kmpc_atomic_float16_div_cpt_rev
    __kmpc_atomic_float16_div_rev
    __kmpc_atomic_float16_max
    __kmpc_atomic_float16_max_cpt
    __kmpc_atomic_float16_min
    __kmpc_atomic_float16_min_cpt
    __kmpc_atomic_float16_mul
    __kmpc_atomic_float16_mul_cpt
    __kmpc_atomic_float16_rd
    __kmpc_atomic_float16_sub
    __kmpc_atomic_float16_sub_cpt
    __kmpc_atomic_float16_sub_cpt_rev
    __kmpc_atomic_float16_sub_rev
    __kmpc_atomic_float16_swp
    __kmpc_atomic_float16_wr
@endcode

Functions for Complex types
---------------------------
Functions for complex types whose component floating point variables are of size
4,8,10 or 16 bytes. The names here are based on the size of the component float,
*not* the size of the complex type. So `__kmpc_atomc_cmplx8_add` is an operation
on a `complex<double>` or `complex(kind=8)`, *not* `complex<float>`.

@code
    __kmpc_atomic_cmplx4_add
    __kmpc_atomic_cmplx4_add_cmplx8
    __kmpc_atomic_cmplx4_add_cpt
    __kmpc_atomic_cmplx4_div
    __kmpc_atomic_cmplx4_div_cmplx8
    __kmpc_atomic_cmplx4_div_cpt
    __kmpc_atomic_cmplx4_div_cpt_rev
    __kmpc_atomic_cmplx4_div_rev
    __kmpc_atomic_cmplx4_mul
    __kmpc_atomic_cmplx4_mul_cmplx8
    __kmpc_atomic_cmplx4_mul_cpt
    __kmpc_atomic_cmplx4_rd
    __kmpc_atomic_cmplx4_sub
    __kmpc_atomic_cmplx4_sub_cmplx8
    __kmpc_atomic_cmplx4_sub_cpt
    __kmpc_atomic_cmplx4_sub_cpt_rev
    __kmpc_atomic_cmplx4_sub_rev
    __kmpc_atomic_cmplx4_swp
    __kmpc_atomic_cmplx4_wr
    __kmpc_atomic_cmplx8_add
    __kmpc_atomic_cmplx8_add_cpt
    __kmpc_atomic_cmplx8_div
    __kmpc_atomic_cmplx8_div_cpt
    __kmpc_atomic_cmplx8_div_cpt_rev
    __kmpc_atomic_cmplx8_div_rev
    __kmpc_atomic_cmplx8_mul
    __kmpc_atomic_cmplx8_mul_cpt
    __kmpc_atomic_cmplx8_rd
    __kmpc_atomic_cmplx8_sub
    __kmpc_atomic_cmplx8_sub_cpt
    __kmpc_atomic_cmplx8_sub_cpt_rev
    __kmpc_atomic_cmplx8_sub_rev
    __kmpc_atomic_cmplx8_swp
    __kmpc_atomic_cmplx8_wr
    __kmpc_atomic_cmplx10_add
    __kmpc_atomic_cmplx10_add_cpt
    __kmpc_atomic_cmplx10_div
    __kmpc_atomic_cmplx10_div_cpt
    __kmpc_atomic_cmplx10_div_cpt_rev
    __kmpc_atomic_cmplx10_div_rev
    __kmpc_atomic_cmplx10_mul
    __kmpc_atomic_cmplx10_mul_cpt
    __kmpc_atomic_cmplx10_rd
    __kmpc_atomic_cmplx10_sub
    __kmpc_atomic_cmplx10_sub_cpt
    __kmpc_atomic_cmplx10_sub_cpt_rev
    __kmpc_atomic_cmplx10_sub_rev
    __kmpc_atomic_cmplx10_swp
    __kmpc_atomic_cmplx10_wr
    __kmpc_atomic_cmplx16_add
    __kmpc_atomic_cmplx16_add_cpt
    __kmpc_atomic_cmplx16_div
    __kmpc_atomic_cmplx16_div_cpt
    __kmpc_atomic_cmplx16_div_cpt_rev
    __kmpc_atomic_cmplx16_div_rev
    __kmpc_atomic_cmplx16_mul
    __kmpc_atomic_cmplx16_mul_cpt
    __kmpc_atomic_cmplx16_rd
    __kmpc_atomic_cmplx16_sub
    __kmpc_atomic_cmplx16_sub_cpt
    __kmpc_atomic_cmplx16_sub_cpt_rev
    __kmpc_atomic_cmplx16_swp
    __kmpc_atomic_cmplx16_wr
@endcode
*/

#include <atomic>
#include <cstdint>
#include "interface.h"
#include "mlfsr32.h" /* For random backoffs */

#if (0)
// Here temporarily... we may need something like this (though we're already relying
// on __uint128_t and atomic<> for the double complex cases below).
typedef __uint128_t uint128_t;

typedef union intValue {
  struct {
    uintptr_t p1;
    uintptr_t p2;
  };
  uint128_t intValue;
} pointerPair_t;
bool cas128(pointerPair_t * old, pointerPair_t & expected,
            pointerPair_t & desired) {
  std::atomic<uint128_t> * ap = (std::atomic<uint128_t> *)old;

  return ap->compare_exchange_strong(expected.intValue, desired.intValue);
}
#endif

namespace lomp {

#define expandInlineBinaryOp(type, typetag, op, optag, reversed)               \
  void __kmpc_atomic_##typetag##_##optag(ident_t *, int *, type * target,      \
                                         type operand) {                       \
    std::atomic<type> * t = (std::atomic<type> *)target;                       \
    *t op## = operand;                                                         \
  }

template <typename T>
union bitRep {
  alignas(sizeof(T)) T typeValue;
  typename typeTraits_t<T>::uint_t uintValue;
  // Need empty constructor and destructor since std::complex<double>
  // causes a hidden destructor problem otherwise.
  // (Why it is different from complex<float> is unclear!)
  bitRep() {}
  ~bitRep() {}
};

// Note that "reversed" here is compile time known, so the test
// should not occur at runtime.
#define expandCasBinaryOp(type, typetag, mutator, optag, reversed)             \
  void __kmpc_atomic_##typetag##_##optag(ident_t *, int *, type * target,      \
                                         type operand) {                       \
    typedef typename typeTraits_t<type>::uint_t unsignedType;                  \
    typedef typename std::atomic<unsignedType> atomicType;                     \
    atomicType * t = (atomicType *)target;                                     \
    typedef bitRep<type> sharedBits;                                           \
                                                                               \
    sharedBits current;                                                        \
    sharedBits next;                                                           \
                                                                               \
    current.uintValue = *t;                                                    \
    if (reversed)                                                              \
      next.typeValue = mutator(operand, current.typeValue);                    \
    else                                                                       \
      next.typeValue = mutator(current.typeValue, operand);                    \
    if (t->compare_exchange_strong(current.uintValue, next.uintValue))         \
      return;                                                                  \
                                                                               \
    if (reversed)                                                              \
      next.typeValue = mutator(operand, current.typeValue);                    \
    else                                                                       \
      next.typeValue = mutator(current.typeValue, operand);                    \
    if (t->compare_exchange_strong(current.uintValue, next.uintValue))         \
      return;                                                                  \
                                                                               \
    randomExponentialBackoff backoff;                                          \
    for (;;) {                                                                 \
      backoff.sleep();                                                         \
      current.uintValue = *t;                                                  \
      if (reversed)                                                            \
        next.typeValue = mutator(operand, current.typeValue);                  \
      else                                                                     \
        next.typeValue = mutator(current.typeValue, operand);                  \
      if (t->compare_exchange_strong(current.uintValue, next.uintValue))       \
        return;                                                                \
                                                                               \
      if (reversed)                                                            \
        next.typeValue = mutator(operand, current.typeValue);                  \
      else                                                                     \
        next.typeValue = mutator(current.typeValue, operand);                  \
      if (t->compare_exchange_strong(current.uintValue, next.uintValue))       \
        return;                                                                \
    }                                                                          \
  }

#define expandCasCheckedOp(type, typetag, function, optag)                     \
  void __kmpc_atomic_##typetag##_##optag(ident_t *, int *, type * target,      \
                                         type operand) {                       \
    typedef typename typeTraits_t<type>::uint_t unsignedType;                  \
    typedef typename std::atomic<unsignedType> atomicType;                     \
    typedef bitRep<type> sharedBits;                                           \
    atomicType * t = (atomicType *)target;                                     \
                                                                               \
    sharedBits current;                                                        \
    sharedBits next;                                                           \
                                                                               \
    current.uintValue = *t;                                                    \
    next.typeValue = function(current.typeValue, operand);                     \
    if (next.typeValue == current.typeValue ||                                 \
        t->compare_exchange_strong(current.uintValue, next.uintValue))         \
      return;                                                                  \
                                                                               \
    next.typeValue = function(current.typeValue, operand);                     \
    if (next.typeValue == current.typeValue ||                                 \
        t->compare_exchange_strong(current.uintValue, next.uintValue))         \
      return;                                                                  \
                                                                               \
    randomExponentialBackoff backoff;                                          \
    for (;;) {                                                                 \
      backoff.sleep();                                                         \
      current.uintValue = *t;                                                  \
      next.typeValue = function(current.typeValue, operand);                   \
      if (next.typeValue == current.typeValue ||                               \
          t->compare_exchange_strong(current.uintValue, next.uintValue))       \
        return;                                                                \
                                                                               \
      next.typeValue = function(current.typeValue, operand);                   \
      if (next.typeValue == current.typeValue ||                               \
          t->compare_exchange_strong(current.uintValue, next.uintValue))       \
        return;                                                                \
    }                                                                          \
  }

// clang-format off
// Don't want any of these turned into two per line, which is hard to read and edit
//
// Operations which can be expressed as std::atomic<type> operator op=(type operand).

// The choices of macros here are somewhat determined by what makes sense
// for integers vs floats, and, also what implementation is possible.
// For instance C++ (<C++20) does not have float atomics, multiply/divide,
// or && and || atomics on any type.
#define FOREACH_ADD_OPERATION(macro, type, typetag)     \
  macro(type, typetag, +, add, false)                   \
  macro(type, typetag, -, sub, false)                   

#define FOREACH_MUL_OPERATION(macro, type, typetag)     \
  macro(type, typetag, doMul, mul, false)               \
  macro(type, typetag, doDiv, div, false)               \
  macro(type, typetag, doSub, sub_rev, true)            \
  macro(type, typetag, doDiv, div_rev, true)
  
#define FOREACH_BITLOGICAL_OPERATION(macro, type, typetag)      \
  macro(type, typetag, &, andb, false)                          \
  macro(type, typetag, |, orb, false)                           \
  macro(type, typetag, ^, xor, false)

// Operators not supoorted as op= by std::atomic<int>
// We'll need to do them with CAS
#define FOREACH_SHIFT_OPERATION(macro, type, typetag)   \
  macro(type, typetag, doShiftLeft, shl, false)         \
  macro(type, typetag, doShiftRight, shr, false)        \
  macro(type, typetag, doShiftLeft, shl_rev, true)      \
  macro(type, typetag, doShiftRight, shr_rev, true)                         

#define FOREACH_LOGICAL_OPERATION(macro, type, typetag) \
  macro(type, typetag, doLogAnd, andl)                  \
  macro(type, typetag, doLogOr,   orl )                            

#define FOREACH_EXTREME_OPERATION(macro, type, typetag) \
  macro(type, typetag, std::min, min )                  \
  macro(type, typetag, std::max, max )

// Need functions for all of the CAS operations, even if they
// could be expressed as a simple operation.
#define generateOperatorFunction(name, op)              \
template<typename T> T do##name(T current, T operand) { \
  return current op operand;                            \
}

#define FOREACH_OPERATOR(macro)                 \
  macro(Add,+)                                  \
  macro(Sub,-)                                  \
  macro(Mul, *)                                 \
  macro(Div, /)                                 \
  macro(ShiftRight, >>)                         \
  macro(ShiftLeft, <<)                          \
  macro(BitAnd, &)                              \
  macro(BitOr,  |)                              \
  macro(Xor, ^)                                 \
  macro(LogAnd, &&)                             \
  macro(LogOr, ||)

// Create all of the functions to encapsulate operators
FOREACH_OPERATOR(generateOperatorFunction)

// FP operations all need CAS (until C++20)
// These are
//  +, -
//  *, /, reversed - reversed /
//  max, min
#define FOREACH_FP_OPERATION(macro, type, typetag)      \
  macro(type, typetag, doAdd, add, false)               \
  macro(type, typetag, doSub, sub, false)               \
  FOREACH_MUL_OPERATION(macro,type,typetag)

#define FOREACH_STD_INTEGER_TYPE(expansionMacro, leafMacro)     \
  expansionMacro(leafMacro, int8_t, fixed1)                     \
  expansionMacro(leafMacro, uint8_t, fixed1u)                   \
  expansionMacro(leafMacro, int16_t, fixed2)                    \
  expansionMacro(leafMacro, uint16_t, fixed2u)                  \
  expansionMacro(leafMacro, int32_t, fixed4)                    \
  expansionMacro(leafMacro, uint32_t, fixed4u)                  \
  expansionMacro(leafMacro, int64_t, fixed8)                    \
  expansionMacro(leafMacro, uint64_t, fixed8u)

#define FOREACH_FP_TYPE(expansionMacro, leafMacro)      \
  expansionMacro(leafMacro, float, float4)              \
  expansionMacro(leafMacro, double, float8)

#define FOREACH_COMPLEX_TYPE(expansionMacro, leafMacro)         \
  expansionMacro(leafMacro, std::complex<float>, cmplx4)        \
  expansionMacro(leafMacro, std::complex<double>, cmplx8)
// clang-format on

// complex<double> needs __uint128_t. All of the compilers seem to
// support that for our targets, and the associated
// std::atomic<__uint128_t>, though GCC does not inline the "lock;
// cmpxchg16b" on X86_64...  (Conclusion: use LLVM, which does when
// given the -mcx16 flag).
// ***TODO*** check alignment issues. If the complex<double> is
// only alignas(8), then the atomic may be very slow when crossing
// a cache line.

/* ***TODO***: capture versions of operations, reads and writes with no operator,
 * swap (exchange).
 */

extern "C" {
// Expand all of the integer cases.
FOREACH_STD_INTEGER_TYPE(FOREACH_ADD_OPERATION, expandInlineBinaryOp)
FOREACH_STD_INTEGER_TYPE(FOREACH_BITLOGICAL_OPERATION, expandInlineBinaryOp)
FOREACH_STD_INTEGER_TYPE(FOREACH_SHIFT_OPERATION, expandCasBinaryOp)
FOREACH_STD_INTEGER_TYPE(FOREACH_MUL_OPERATION, expandCasBinaryOp)
FOREACH_STD_INTEGER_TYPE(FOREACH_LOGICAL_OPERATION, expandCasCheckedOp)
FOREACH_STD_INTEGER_TYPE(FOREACH_EXTREME_OPERATION, expandCasCheckedOp)

// Expand all of the floating point cases
FOREACH_FP_TYPE(FOREACH_FP_OPERATION, expandCasBinaryOp)
FOREACH_FP_TYPE(FOREACH_EXTREME_OPERATION, expandCasCheckedOp)

// Complex operations Work OK with clang 9.0, but not with the (old,
// gcc 4.8.5) headers I find on Linux.
#if (GENERATE_COMPLEX_ATOMICS)
FOREACH_COMPLEX_TYPE(FOREACH_FP_OPERATION, expandCasBinaryOp)
#endif
}
} // namespace lomp
