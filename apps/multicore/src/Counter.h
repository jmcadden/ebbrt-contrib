//          Copyright Boston University SESA Group 2013 - 2016.
//
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef EBBRT_ZKCOUNTER_H_
#define EBBRT_ZKCOUNTER_H_

#include "MulticoreEbb.h"
#include <ebbrt/LocalIdMap.h>

namespace ebbrt {


class Counter;
class GCounter;

class CounterRoot : public ebbrt::MulticoreEbbRoot<CounterRoot, Counter> {
  using MulticoreEbbRoot<CounterRoot, Counter>::MulticoreEbbRoot;

public:
  CounterRoot(ebbrt::EbbId id, int init)
      : MulticoreEbbRoot<CounterRoot, Counter>(id), init_(init) {}
  uint64_t Get();
private:
  int init_ = 0;
};

class Counter : public ebbrt::MulticoreEbb<Counter, CounterRoot> {
public:
  // implicit use of template constructors
  using MulticoreEbb<Counter, CounterRoot>::MulticoreEbb;
  static ebbrt::EbbRef<Counter>
  Create(int init, ebbrt::EbbId id = ebbrt::ebb_allocator->Allocate()) {
    LocalIdMap::Accessor accessor;
    auto created = local_id_map->Insert(accessor, id);
    if (!created) // map entry should be empty
      ebbrt::kabort();
    accessor->second = new CounterRoot(id, init);
    return ebbrt::EbbRef<Counter>(id);
  }
  void Up();
  void Down();
  uint64_t GetLocal();
  uint64_t Get();

private:
  uint64_t count_ = 0;
};

class GCounterRoot : public ebbrt::MulticoreEbbRoot<GCounterRoot, GCounter> {
  using MulticoreEbbRoot<GCounterRoot, GCounter>::MulticoreEbbRoot;
public:
  uint64_t Get();
};

class GCounter : public Counter, public ebbrt::MulticoreEbb<GCounter, GCounterRoot>{
public:
  using MulticoreEbb<GCounter, GCounterRoot>::HandleFault;
  using MulticoreEbb<GCounter, GCounterRoot>::root_;
  using Counter::Counter;
  const static ebbrt::EbbId GlobalCounterId = 321;
  uint64_t Get();

  GCounter(GCounterRoot* root)
      : Counter((CounterRoot*)root), MulticoreEbb<GCounter, GCounterRoot>(root) {};
};

};     // end namespace
#endif // EBBRT_ZKGLOBALIDMAP_H_