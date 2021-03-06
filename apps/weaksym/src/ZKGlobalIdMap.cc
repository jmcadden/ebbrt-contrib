//          Copyright Boston University SESA Group 2013 - 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <cstdio>
#include <string>

#include <ebbrt/Debug.h>
#include "ZKGlobalIdMap.h"

ebbrt::GlobalIdMap::GlobalIdMap() {
    ebbrt::kprintf("ZKGlobalIdMap consturctor called\n");
    }

__attribute__((weak)) void ebbrt::GlobalIdMap::SetAddress(uint32_t addr) {
  ebbrt::kprintf("ZKGlobalIdMap SetAddress() called\n");
}

ebbrt::Future<bool> ebbrt::GlobalIdMap::Init() {
    ebbrt::kprintf("ZKGlobalIdMap init called\n");
  zk_ = ebbrt::ZooKeeper::Create(ebbrt::ebb_allocator->Allocate(), &zkwatcher_);
  return zkwatcher_.connected_.GetFuture();
}

ebbrt::Future<std::string> ebbrt::GlobalIdMap::Get(ebbrt::EbbId id,
                                                     std::string path) {
  ebbrt::kprintf("ZKGlobalIdMap Get() called\n");
  char buff[100];
  sprintf(buff, "/%d", id);
  auto p = new ebbrt::Promise<std::string>;
  auto f = p->GetFuture();
  zk_->Get(std::string(buff)).Then([p](ebbrt::Future<ebbrt::ZooKeeper::Znode> z) {
    auto znode = z.Get();
    p->SetValue(znode.value);
  });
  return f;
}

ebbrt::Future<void> ebbrt::GlobalIdMap::Set(EbbId id, std::string val,
                                              std::string path) {
  ebbrt::kprintf("ZKGlobalIdMap Set() called\n");
  auto p = new ebbrt::Promise<void>;
  auto ret = p->GetFuture();
  char buff[100];
  sprintf(buff, "/%d", id);
  zk_->Exists(std::string(buff)).Then([this, p, buff, val](auto b) {
    if (b.Get() == true) {
      zk_->Set(std::string(buff), val).Then([p](auto f) { p->SetValue(); });
    } else {
      zk_->New(std::string(buff), val).Then([p](auto f) { p->SetValue(); });
    }
  });
  return ret;
}

ebbrt::Future<std::vector<std::string>>
ebbrt::GlobalIdMap::List(ebbrt::EbbId id, std::string path ){
  auto p = new ebbrt::Promise<std::vector<std::string>>;
  auto ret = p->GetFuture();
  char buff[100];
  sprintf(buff, "/%d", id);
  zk_->GetChildren(std::string(buff)).Then([p](auto f){
      auto zn_children = f.Get();
      p->SetValue(zn_children.values);
  });
  return ret;
}

void ebbrt::GlobalIdMap::SetWatcher(EbbId id, Watcher *w, std::string path) {
  char buf[15];
  sprintf(buf, "/%d", id);
  zk_->Stat(std::string(buf), w).Block();
  return;
}
