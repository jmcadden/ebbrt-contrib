//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/Debug.h>
#include <ebbrt/Trace.h>
#include "Printer.h"

#define ITERATIONS 10000
#define INTEL 0

namespace{
const uint32_t reg = 1 << 30;  // Intel fixed-purpose-register config flag
}

void do_nothing() {
  asm volatile ("");
}

uint64_t do_rdtsc(){
  return ebbrt::trace::rdtsc();
}

#if INTEL
uint64_t do_perf(){
  return ebbrt::trace::rdpmc((reg));
}
#endif

void AppMain() {
  printer->Print("TRACE START.\n");

  uint64_t ts = 0, is = 0, cs = 0, te = 0, ie = 0, ce = 0;
  uint64_t tave, iave, cave;
  ebbrt::trace::Init();
#if INTEL
  if(!ebbrt::trace::IsIntel()){
    ebbrt::kabort("This code only supports Intel VT-x");
  }
#endif

  // MEASURMENT 1 - COST OF NOTHING
  //warmup
  for (auto i = 0; i < ITERATIONS; i++) { do_nothing(); }
#if INTEL
  is = ebbrt::trace::rdpmc((reg));
  cs = ebbrt::trace::rdpmc((reg + 1));
#endif
  ts = ebbrt::trace::rdtsc();
  for (auto i = 0; i < ITERATIONS; i++) { do_nothing(); }
  te = ebbrt::trace::rdtsc();
#if INTEL
  ce = ebbrt::trace::rdpmc((reg + 1));
  ie = ebbrt::trace::rdpmc((reg));
#endif
  tave = (te - ts) / ITERATIONS;
  iave = (ie - is) / ITERATIONS;
  cave = (ce - cs) / ITERATIONS;
  ebbrt::kprintf("NOP: timeclock total:%llu ave:%llu\n",(te - ts), tave);
  ebbrt::kprintf("NOP: instruction total:%llu ave:%llu\n",(ie - is), iave);
  ebbrt::kprintf("NOP: cycle total:%llu ave:%llu\n", (ce - cs), cave);

  // MEASURMENT 2 - COST OF WALLCLOCK
  // warmup
  for (auto i = 0; i < ITERATIONS; i++) { do_rdtsc(); }
#if INTEL
  is = ebbrt::trace::rdpmc((reg));
  cs = ebbrt::trace::rdpmc((reg + 1));
#endif
  ts = ebbrt::trace::rdtsc();
  for (auto i = 0; i < ITERATIONS; i++) { do_rdtsc(); }
  te = ebbrt::trace::rdtsc();
#if INTEL
  ce = ebbrt::trace::rdpmc((reg + 1));
  ie = ebbrt::trace::rdpmc((reg));
#endif
  tave = (te - ts) / ITERATIONS;
  iave = (ie - is) / ITERATIONS;
  cave = (ce - cs) / ITERATIONS;
  ebbrt::kprintf("RDTSC: timeclock total:%llu ave:%llu\n",(te - ts), tave);
  ebbrt::kprintf("RDTSC: instruction total:%llu ave:%llu\n",(ie - is), iave);
  ebbrt::kprintf("RDTSC: cycle total:%llu ave:%llu\n", (ce - cs), cave);

#if INTEL
  // MEASURMENT 3 - COST OF PERFORMANCE COUNTERS
  for (auto i = 0; i < ITERATIONS; i++) { do_perf(); }
  is = ebbrt::trace::rdpmc((reg));
  cs = ebbrt::trace::rdpmc((reg + 1));
  ts = ebbrt::trace::rdtsc();
  for (auto i = 0; i < ITERATIONS; i++) { do_perf(); }
  te = ebbrt::trace::rdtsc();
  ce = ebbrt::trace::rdpmc((reg + 1));
  ie = ebbrt::trace::rdpmc((reg));
  tave = (te - ts) / ITERATIONS;
  iave = (ie - is) / ITERATIONS;
  cave = (ce - cs) / ITERATIONS;
  ebbrt::kprintf("PCs: timeclock total:%llu ave:%llu\n",(te - ts), tave);
  ebbrt::kprintf("PCs: instruction total:%llu ave:%llu\n",(ie - is), iave);
  ebbrt::kprintf("PCs: cycle total:%llu ave:%llu\n", (ce - cs), cave);
#endif

  // MEASURMENT 4 - COST OF A TRACEPOINT
#if INTEL
  is = ebbrt::trace::rdpmc((reg));
  cs = ebbrt::trace::rdpmc((reg + 1));
#endif
  ts = ebbrt::trace::rdtsc();
  for (auto i = 0; i < ITERATIONS; i++) { ebbrt::trace::AddTracepoint(0);}
  te = ebbrt::trace::rdtsc();
#if INTEL
  ce = ebbrt::trace::rdpmc((reg + 1));
  ie = ebbrt::trace::rdpmc((reg));
#endif
  tave = (te - ts) / ITERATIONS;
  iave = (ie - is) / ITERATIONS;
  cave = (ce - cs) / ITERATIONS;
  ebbrt::kprintf("TP: timeclock total:%llu ave:%llu\n",(te - ts), tave);
  ebbrt::kprintf("TP: instruction total:%llu ave:%llu\n",(ie - is), iave);
  ebbrt::kprintf("TP: cycle total:%llu ave:%llu\n", (ce - cs), cave);

  // MEASURMENT 5 - COST OF A TRACENOTE
#if INTEL
  is = ebbrt::trace::rdpmc((reg));
  cs = ebbrt::trace::rdpmc((reg + 1));
#endif
  ts = ebbrt::trace::rdtsc();
  for (auto i = 0; i < ITERATIONS; i++) { ebbrt::trace::AddNote("overhead");}
  te = ebbrt::trace::rdtsc();
#if INTEL
  ce = ebbrt::trace::rdpmc((reg + 1));
  ie = ebbrt::trace::rdpmc((reg));
#endif
  tave = (te - ts) / ITERATIONS;
  iave = (ie - is) / ITERATIONS;
  cave = (ce - cs) / ITERATIONS;
  ebbrt::kprintf("TN: timeclock total:%llu ave:%llu\n",(te - ts), tave);
  ebbrt::kprintf("TN: instruction total:%llu ave:%llu\n",(ie - is), iave);
  ebbrt::kprintf("TN: cycle total:%llu ave:%llu\n", (ce - cs), cave);

  // Ebb Test
  ebbrt::trace::Enable();
  printer->Print("TRACE FINISHED.\n");
  ebbrt::trace::Disable();

  ebbrt::trace::Dump();

}
