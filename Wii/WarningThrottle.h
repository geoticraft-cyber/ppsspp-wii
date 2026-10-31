// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <limits>
namespace Wii {
// Keep first occurrences and logarithmic summaries. A repeated warning must
// not force a synchronous log-file flush for every MPEG packet on slow hosts.
inline bool CountRepeatedWarning(unsigned &count) {
  if(count!=std::numeric_limits<unsigned>::max())++count;
  return count<=4 || !(count&(count-1));
}
}
