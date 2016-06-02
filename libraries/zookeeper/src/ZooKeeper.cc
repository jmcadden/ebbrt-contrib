//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
//
//
#include "ZooKeeper.h"
#include <ebbrt/Timer.h>
#include <poll.h>
#include <poll.h>
#include <zookeeper.h>

void ebbrt::ZooKeeper::process_watch_event(zhandle_t *h, int type, int state,
                                           const char *path, void *ctx) {
  auto watcher = static_cast<Watcher *>(ctx);
  watcher->WatchHandler(type, state, path);
}

ebbrt::ZooKeeper::ZooKeeper(const std::string &server_hosts,
                            ebbrt::ZooKeeper::Watcher *connection_watcher,
                            int timeout_ms)
    : connection_watcher_(connection_watcher) {

  // create zk object
  zoo_set_debug_level(ZOO_LOG_LEVEL_DEBUG);
  zoo_deterministic_conn_order(1); // deterministic command->server assignment
  zk_ = zookeeper_init(server_hosts.c_str(), process_watch_event, timeout_ms,
                       nullptr, connection_watcher, 0);
  timer->Start(*this, std::chrono::milliseconds(750), true);
  return;
}

ebbrt::ZooKeeper::~ZooKeeper() {
  if (zk_) {
    auto ret = zookeeper_close(zk_);
    if (ret != ZOK) {
      ebbrt::kabort("Zookeeper close error: %d\n", ret);
    }
  }
}

/* IO Hanlder */
void ebbrt::ZooKeeper::Fire() {
  std::lock_guard<ebbrt::SpinLock> guard(lock_);
  struct pollfd fds[1];

  if (zk_) {
    struct timeval tv;
    int fd;
    int interest;
    int timeout;
    int maxfd = 1;
    int rc;

    rc = zookeeper_interest(zk_, &fd, &interest, &tv);
    if (rc != ZOK) {
      ebbrt::kabort("zookeeper_interest error");
    }
    if (fd != -1) {
      fds[0].fd = fd;
      fds[0].events = (interest & ZOOKEEPER_READ) ? POLLIN : 0;
      fds[0].events |= (interest & ZOOKEEPER_WRITE) ? POLLOUT : 0;
      maxfd = 1;
      fds[0].revents = 0;
    }
    timeout = tv.tv_sec * 1000 + (tv.tv_usec / 1000);

    poll(fds, maxfd, timeout);
    if (fd != -1) {
      interest = (fds[0].revents & POLLIN) ? ZOOKEEPER_READ : 0;
      interest |= ((fds[0].revents & POLLOUT) || (fds[0].revents & POLLHUP))
                      ? ZOOKEEPER_WRITE
                      : 0;
    }
    zookeeper_process(zk_, interest);
    if (rc != ZOK) {
      ebbrt::kabort("zookeeper_process error");
    }
    if (is_unrecoverable(zk_)) {
      //  api_epilog(zk_, 0);
      ebbrt::kabort(("zookeeper io handler  terminated"));
    }
  }
}

ebbrt::Future<ebbrt::ZooKeeper::ZkResponse>
ebbrt::ZooKeeper::Create(const std::string &path, const std::string &value,
                         int flags) {
  auto callback = [](int rc, const char *value, const void *data) {
    ZkResponse res;
    res.err = rc;
    if (value)
      res.value = std::string(value);
    auto p =
        static_cast<ebbrt::Promise<ZkResponse> *>(const_cast<void *>(data));
    p->SetValue(std::move(res));
    delete p;
    return;
  };

  auto p = new ebbrt::Promise<ZkResponse>;
  auto f = p->GetFuture();

  // TODO: verify this work for empty string (i.e., c_str() = "" or nullptr)
  zoo_acreate(zk_, path.c_str(), value.c_str(), value.size(),
              &ZOO_OPEN_ACL_UNSAFE, flags, callback, p);
  return f;
}

ebbrt::Future<ebbrt::ZooKeeper::ZkResponse>
ebbrt::ZooKeeper::Exists(const std::string &path,
                         ebbrt::ZooKeeper::Watcher *watcher) {
  auto callback = [](int rc, const ZkStat *stat, const void *data) {
    ZkResponse res;
    res.err = rc;
    res.stat = *stat;
    auto p =
        static_cast<ebbrt::Promise<ZkResponse> *>(const_cast<void *>(data));
    p->SetValue(std::move(res));
    delete p;
    return;
  };

  auto p = new ebbrt::Promise<ZkResponse>;
  auto f = p->GetFuture();

  if (watcher) {
    zoo_awexists(zk_, path.c_str(), process_watch_event, watcher, callback, p);
  } else {
    zoo_aexists(zk_, path.c_str(), 0, callback, p);
  }
  return f;
}

ebbrt::Future<ebbrt::ZooKeeper::ZkResponse>
ebbrt::ZooKeeper::Get(const std::string &path,
                      ebbrt::ZooKeeper::Watcher *watcher) {
  auto callback = [](int rc, const char *value, int value_len,
                     const ZkStat *stat, const void *data) {
    ZkResponse res;
    res.err = rc;
    res.stat = *stat;
    if (value_len > 0)
      res.value = std::string(value, static_cast<size_t>(value_len));
    auto p =
        static_cast<ebbrt::Promise<ZkResponse> *>(const_cast<void *>(data));
    p->SetValue(std::move(res));
    delete p;
    return;
  };

  auto p = new ebbrt::Promise<ZkResponse>;
  auto f = p->GetFuture();

  if (watcher) {
    zoo_awget(zk_, path.c_str(), process_watch_event, watcher, callback, p);
  } else {
    zoo_aget(zk_, path.c_str(), 0, callback, p);
  }
  return f;
}

ebbrt::Future<ebbrt::ZooKeeper::ZkChildrenResponse>
ebbrt::ZooKeeper::GetChildren(const std::string &path,
                              ebbrt::ZooKeeper::Watcher *watcher) {
  auto callback = [](int rc, const struct String_vector *strings,
                     const ZkStat *stat, const void *data) {
    ZkChildrenResponse res;
    res.err = rc;
    res.stat = *stat;

    if (strings) {
      for (int i = 0; i < strings->count; ++i) {
        res.values.emplace_back(std::string(strings->data[i]));
      }
    }

    auto p = static_cast<ebbrt::Promise<ZkChildrenResponse> *>(
        const_cast<void *>(data));
    p->SetValue(std::move(res));
    delete p;
    return;
  };

  auto p = new ebbrt::Promise<ZkChildrenResponse>;
  auto f = p->GetFuture();

  if (watcher) {
    zoo_awget_children2(zk_, path.c_str(), process_watch_event, watcher,
                        callback, p);
  } else {
    zoo_aget_children2(zk_, path.c_str(), 0, callback, p);
  }
  return f;
}

ebbrt::Future<ebbrt::ZooKeeper::ZkResponse>
ebbrt::ZooKeeper::Delete(const std::string &path, int version) {
  auto callback = [](int rc, const void *data) {
    ZkResponse res;
    res.err = rc;
    auto p =
        static_cast<ebbrt::Promise<ZkResponse> *>(const_cast<void *>(data));
    p->SetValue(std::move(res));
    delete p;
    return;
  };

  auto p = new ebbrt::Promise<ZkResponse>;
  auto f = p->GetFuture();

  zoo_adelete(zk_, path.c_str(), version, callback, p);
  return f;
}

ebbrt::Future<ebbrt::ZooKeeper::ZkResponse>
ebbrt::ZooKeeper::Set(const std::string &path, const std::string &value,
                      int version) {
  auto callback = [](int rc, const ZkStat *stat, const void *data) {
    ZkResponse res;
    res.err = rc;
    res.stat = *stat;
    auto p =
        static_cast<ebbrt::Promise<ZkResponse> *>(const_cast<void *>(data));
    p->SetValue(std::move(res));
    delete p;
    return;
  };

  auto p = new ebbrt::Promise<ZkResponse>;
  auto f = p->GetFuture();

  zoo_aset(zk_, path.c_str(), value.c_str(), value.size(), version, callback,
           p);
  return f;
}

void ebbrt::ZooKeeper::print_stat( ZkStat *stat) {
  ebbrt::kprintf("Stat: \n");
  ebbrt::kprintf("     version: %d \n", stat->version);
  ebbrt::kprintf("     data len: %d \n", stat->dataLength);
  ebbrt::kprintf("     children: %d \n", stat->numChildren);
  ebbrt::kprintf("     eph owner: %ld \n", stat->ephemeralOwner);
  ebbrt::kprintf("\n");
    // todo timestamp
  return;
}

void ebbrt::ZooKeeper::PrintRecord( ZkResponse *zkr) {
  if( !zkr )
    return;

  if( zkr->err != ZOK )
    ebbrt::kprintf("Operation Error: %d\n", zkr->err);
  //else
  //  ebbrt::kprintf("Operation Successful\n",);
  if( zkr->value.size() > 0 ) 
    ebbrt::kprintf("Value: %s\n", zkr->value.c_str());
  else
    ebbrt::kprintf("Value: NO VALUE\n");

  print_stat(&(zkr->stat));

  return;
}

void ebbrt::ZooKeeper::PrintDirectory(ZkChildrenResponse *zkr) {
  if( !zkr )
    return;
  if( zkr->err != ZOK )
    ebbrt::kprintf("Operation Error: %d\n", zkr->err);
    ebbrt::kprintf("Values: \n");
  for(auto i : zkr->values){
    ebbrt::kprintf("    %s\n",i.c_str());
  }
  return;
}

/* Command-line Interface - processes command, results sent to the stdout */
void ebbrt::ZooKeeper::CLI(char *line) {
  int rc =0;
  if (startsWith(line, "help")) {
    ebbrt::kprintf("    create [+[e|s]] <path>\n");
    ebbrt::kprintf("    delete <path>\n");
    ebbrt::kprintf("    exists <path>\n");
    ebbrt::kprintf("    get <path>\n");
    ebbrt::kprintf("    set <path> <data>\n");
    ebbrt::kprintf("    ls <path>\n");
    ebbrt::kprintf("    quit\n");
    ebbrt::kprintf("\n");
  } else if (startsWith(line, "get ")) {
    line += 4;
    if (line[0] != '/') {
      ebbrt::kprintf("Path must start with /, found: %s\n", line);
      return;
    }
    std::string path(line);
    auto fp = Get(path);
    auto f = fp.Block().Get();
    PrintRecord(&f);
  } else if (startsWith(line, "exists ")) {
    line += 7;
    if (line[0] != '/') {
      ebbrt::kprintf("Path must start with /, found: %s\n", line);
      return;
    }
    std::string path(line);
    auto fp = Exists(path);
    auto f = fp.Block().Get();
    PrintRecord(&f);
  } else if (startsWith(line, "set ")) {
    char *ptr;
    line += 4;
    if (line[0] != '/') {
      ebbrt::kprintf("Path must start with /, found: %s\n", line);
      return;
    }
    ptr = strchr(line, ' ');
    if (!ptr) {
      ebbrt::kprintf("No data found after path\n");
      return;
    }
    *ptr = '\0';
    ptr++;
    std::string path(line);
    std::string value(ptr);
    auto fp = Set(path, value);
    auto f = fp.Block().Get();
    PrintRecord(&f);
  } else if (startsWith(line, "ls ")) {
    line += 3;
    if (line[0] != '/') {
      ebbrt::kprintf("Path must start with /, found: %s\n", line);
      return;
    }
    std::string path(line);
    auto fp = GetChildren(path);
    auto f = fp.Block().Get();
    PrintDirectory(&f);

    if (rc) {
      ebbrt::kprintf("Error %d for %s\n", rc, line);
    }
  } else if (startsWith(line, "create ")) {
    int flags = 0;
    line += 7;
    if (line[0] == '+') {
      line++;
      if (line[0] == 'e') {
        flags |= ZOO_EPHEMERAL; 
        line++;
      }
      if (line[0] == 's') {
        flags |= ZOO_SEQUENCE;
        line++;
      }
      line++;
    }
    if (line[0] != '/') {
      ebbrt::kprintf("Path must start with /, found: %s\n", line);
      return;
    }
    ebbrt::kprintf("Creating [%s] node\n", line);
    std::string path(line);
    auto fp = Create(path, "new", flags);
    auto f = fp.Block().Get();
    PrintRecord(&f);
  } else if (startsWith(line, "delete ")) {
    line += 7;
    if (line[0] != '/') {
      ebbrt::kprintf("Path must start with /, found: %s\n", line);
      return;
    }
    std::string path(line);
    auto fp = Delete(path);
    auto f = fp.Block().Get();
    PrintRecord(&f);
  }else{
    ebbrt::kprintf("Error unknown command: %s\n", line);
  }
}


void ebbrt::ZooKeeper::my_string_completion_free_data(int rc, const char *name,
                                                      const void *data) {
}

void ebbrt::ZooKeeper::my_data_completion(int rc, const char *value,
                                          int value_len, const ZkStat *stat,
                                          const void *data) {
}

void ebbrt::ZooKeeper::my_silent_data_completion(int rc, const char *value,
                                                 int value_len,
                                                 const ZkStat *stat,
                                                 const void *data) {
}

void ebbrt::ZooKeeper::my_strings_completion(
    int rc, const struct String_vector *strings, const void *data) {
}

void ebbrt::ZooKeeper::my_void_completion(int rc, const void *data) {
}

void ebbrt::ZooKeeper::my_stat_completion(int rc, const ZkStat *stat,
                                          const void *data) {
}

void ebbrt::ZooKeeper::my_silent_stat_completion(int rc, const ZkStat *stat,
                                                 const void *data) {
}

int ebbrt::ZooKeeper::startsWith(const char *line, const char *prefix) {
  int len = strlen(prefix);
  return strncmp(line, prefix, len) == 0;
}
