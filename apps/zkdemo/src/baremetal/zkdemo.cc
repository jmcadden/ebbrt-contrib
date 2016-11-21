//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/Debug.h>

#include "Printer.h"
#include "../ZKGlobalIdMap.h"

void AppMain() { 
  
  printer->Print("ZKDEMO BACKEND UP.\n"); 
  ebbrt::zkglobal_id_map->Init().Then([](auto connected) {
   ebbrt::kbugon(connected.Get() == false);
   ebbrt::kprintf("getting secret\n");
   auto val = ebbrt::zkglobal_id_map->Get(42).Block().Get();
   ebbrt::kprintf("The secret value is %s\n", val.c_str());
  });
}
