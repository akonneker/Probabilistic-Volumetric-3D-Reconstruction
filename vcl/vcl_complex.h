#ifndef vcl_complex_h_
#define vcl_complex_h_

#include <vcl/vcl_compiler.h>

// File: vcl_complex.h
// 
// The task of this horrible file is to rationalize the complex number
// support in the various compilers.  Basically it promises to give you:
//
// A working vcl_complex<T> template.
//
// The typedefs vcl_float_complex, vcl_double_complex.
//
// Stream >> and << operators.
//
// Instances of the types vcl_complex<float> and vcl_complex<double>
//
// A macro VCL_COMPLEX_INSTANTIATE(T) which allows you to instantiate
// complex over other number types.
//
// If you just want to forward declare the vcl complex types, use 
// vcl_complex_fwd.h instead.
#include <vcl/vcl_complex_fwd.h>

// ---------- all emulation
#if !VCL_USE_NATIVE_COMPLEX 
# include <vcl/emulation/vcl_complex.h>
# define vcl_complex_STD /*std::*/::

// ---------- egcs
#elif defined(VCL_EGCS)
# include <complex>
# define vcl_complex_STD /*std::*/::

// ---------- gcc 2.95
#elif defined(VCL_GCC_295)
# if !defined(GNU_LIBSTDCXX_V3)
// old library
#  include <complex>
#  define vcl_complex_STD /*std::*/::
# else
// new library (broken complex)
#  include <vcl/emulation/vcl_complex.h>
#  define vcl_complex_STD /*std::*/::
// paradoxically, v3 needs 'abs' and 'sqrt' here because it 
// wants to use the emulation vcl_complex<>.
#  undef  vcl_abs
#  define vcl_abs abs
   using std::abs;
#  undef  vcl_sqrt
#  define vcl_sqrt sqrt
   using std::sqrt;
# endif

// ---------- native WIN32
#elif defined(VCL_WIN32)
# include <vcl/win32/vcl_complex.h>
# define vcl_complex_STD std::

// ---------- SunPro compiler
#elif defined(VCL_SUNPRO_CC)
# include <vcl/sunpro/vcl_complex.h>
# define vcl_complex_STD std::

// ---------- ISO
#else
# include <vcl/iso/vcl_complex.h>
#endif

#ifdef vcl_complex_STD
# ifndef vcl_abs
#  define vcl_abs  vcl_complex_STD abs
# endif
# ifndef vcl_sqrt
#  define vcl_sqrt vcl_complex_STD sqrt
# endif
# define vcl_arg   vcl_complex_STD arg
# define vcl_conj  vcl_complex_STD conj
# define vcl_norm  vcl_complex_STD norm
# define vcl_polar vcl_complex_STD polar
#endif

//--------------------------------------------------------------------------------

// At this point we have vcl_complex<T>, so this should work for all compilers :
typedef vcl_complex<double> vcl_double_complex;
typedef vcl_complex<float>  vcl_float_complex;


// bogus instantiation macro.
#define VCL_COMPLEX_INSTANTIATE(T) extern "you must include vcl_complex.txx instead"

#endif
