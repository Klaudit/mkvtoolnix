/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   math helper functions

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#ifndef __MTX_COMMON_MATH_H
#define __MTX_COMMON_MATH_H

#include "common/os.h"

#define irnd(a) ((int64_t)((double)(a) + 0.5))
#define iabs(a) ((a) < 0 ? (a) * -1 : (a))

uint32_t MTX_DLL_API round_to_nearest_pow2(uint32_t value);

int MTX_DLL_API int_log2(uint32_t value);

#endif  // __MTX_COMMON_MATH_H