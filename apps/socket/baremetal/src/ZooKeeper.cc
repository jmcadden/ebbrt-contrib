//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
//
//
#include <ebbrt/Timer.h>
#include "ZooKeeper.h"
#include <zookeeper.h>
#include <poll.h>
#include <poll.h>

struct timeval startTime;
static const char *hostPort;
#define _LL_CAST_ (long long)

void ebbrt::ZooKeeper::CallWatcher(zhandle_t* h, int type, int state, const char* path, void* ctx) {
  auto watcher = static_cast<Watcher*>(ctx);
  watcher->WatchHandler(type, state, path);
}

ebbrt::ZooKeeper::ZooKeeper(const std::string &server_hosts, ebbrt::ZooKeeper::Watcher* connection_watcher, int timeout_ms) : connection_watcher_(connection_watcher) {

  // create zk object
  zoo_set_debug_level(ZOO_LOG_LEVEL_DEBUG);
  zoo_deterministic_conn_order(1); // enable deterministic order
  zk_ = zookeeper_init(server_hosts.c_str(),
                               CallWatcher,
                               timeout_ms,
                               nullptr, 
                               connection_watcher, 
                               0);
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

ebbrt::Future<ebbrt::ZooKeeper::ZkResponse> ebbrt::ZooKeeper::Create(const std::string &path, const std::string &value, int flags){
  auto callback = [](int rc, const char *value, const void *data) {
    ZkResponse res;
    res.rc = rc;
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

  // TODO: does this work for empty string (i.e., c_str() = nullptr)
  zoo_acreate(zk_, path.c_str(), value.c_str(), value.size(), &ZOO_OPEN_ACL_UNSAFE, flags, callback, p);
  return f;
}

ebbrt::Future<ebbrt::ZooKeeper::ZkResponse> ebbrt::ZooKeeper::Exists(const std::string &path, ebbrt::ZooKeeper::Watcher* watcher){
  auto callback = [](int rc, const ZkStat *stat, const void *data){
    ZkResponse res;
    res.rc = rc;
    res.stat = *stat;
    auto p =
        static_cast<ebbrt::Promise<ZkResponse> *>(const_cast<void *>(data));
    p->SetValue(std::move(res));
    delete p;
    return;
  };

  auto p = new ebbrt::Promise<ZkResponse>;
  auto f = p->GetFuture();

  if(watcher){
   zoo_awexists(zk_, path.c_str(), CallWatcher, watcher, callback, p);
  }else{
   zoo_aexists(zk_, path.c_str(), 0, callback, p);
  }
  return f;
}

ebbrt::Future<ebbrt::ZooKeeper::ZkResponse> ebbrt::ZooKeeper::Get(const std::string &path, ebbrt::ZooKeeper::Watcher* watcher){
  auto callback = [](int rc, const char *value, int value_len,
                       const ZkStat *stat, const void *data) {
    ZkResponse res;
    res.rc = rc;
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

  if(watcher){
   zoo_awget(zk_, path.c_str(), CallWatcher, watcher, callback, p);
  }else{
   zoo_aget(zk_, path.c_str(), 0, callback, p);
  }
  return f;
}

ebbrt::Future<ebbrt::ZooKeeper::ZkChildrenResponse> ebbrt::ZooKeeper::GetChildren(const std::string &path, ebbrt::ZooKeeper::Watcher* watcher){
  auto callback = [](int rc,
        const struct String_vector *strings, const ZkStat *stat,
        const void *data){
    ZkChildrenResponse res;
    res.rc = rc;
    res.stat = *stat;

    if(strings){
      for( int i=0; i< strings->count; ++i){
        res.values.emplace_back(std::string(strings->data[i]));
      }
    }

    auto p =
        static_cast<ebbrt::Promise<ZkChildrenResponse> *>(const_cast<void *>(data));
    p->SetValue(std::move(res));
    delete p;
    return;
  };

  auto p = new ebbrt::Promise<ZkChildrenResponse>;
  auto f = p->GetFuture();

  if(watcher){
   zoo_awget_children2(zk_, path.c_str(), CallWatcher, watcher, callback, p);
  }else{
   zoo_aget_children2(zk_, path.c_str(), 0, callback, p);
  }
  return f;
}

ebbrt::Future<ebbrt::ZooKeeper::ZkResponse> ebbrt::ZooKeeper::Delete(const std::string &path, int version){
  auto callback = [](int rc, const void *data) {
    ZkResponse res;
    res.rc = rc;
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

ebbrt::Future<ebbrt::ZooKeeper::ZkResponse> ebbrt::ZooKeeper::Set(const std::string &path, const std::string &value, int version){
  auto callback = [](int rc, const ZkStat *stat, const void *data){
    ZkResponse res;
    res.rc = rc;
    res.stat = *stat;
    auto p =
        static_cast<ebbrt::Promise<ZkResponse> *>(const_cast<void *>(data));
    p->SetValue(std::move(res));
    delete p;
    return;
  };

  auto p = new ebbrt::Promise<ZkResponse>;
  auto f = p->GetFuture();

   zoo_aset(zk_, path.c_str(), value.c_str(), value.size(), version, callback, p);
  return f;
}


void ebbrt::ZooKeeper::Foo(){

  auto j = Exists("/foo", connection_watcher_);
  auto k = Exists("/bar", connection_watcher_);

  auto jv = j.Block().Get();

  if(jv.rc == ZOK){
    kprintf("Foo exists\n");
  } else{
    kprintf("Foo does not exist\n");
  }

  auto bv = k.Block().Get();

  if(bv.rc == ZOK){
    kprintf("bar exists\n");
  } else{
    kprintf("bar does not exist\n");
  }

}



////////// ////////// ////////// ////////// ////////// ////////// //////////
////////// ////////// ////////// ////////// ////////// ////////// //////////
////////// ////////// ////////// ////////// ////////// ////////// //////////
/// TEMP
////////// ////////// ////////// ////////// ////////// ////////// //////////
////////// ////////// ////////// ////////// ////////// ////////// //////////
////////// ////////// ////////// ////////// ////////// ////////// //////////


void ebbrt::ZooKeeper::dumpstat(const ZkStat *stat) {
    char tctimes[40];
    char tmtimes[40];
    time_t tctime;
    time_t tmtime;

    if (!stat) {
        fprintf(stderr,"null\n");
        return;
    }
    tctime = stat->ctime/1000;
    tmtime = stat->mtime/1000;
       
    ctime_r(&tmtime, tmtimes);
    ctime_r(&tctime, tctimes);
       
    fprintf(stderr, "\tctime = %s\tczxid=%llx\n"
    "\tmtime=%s\tmzxid=%llx\n"
    "\tversion=%x\taversion=%x\n"
    "\tephemeralOwner = %llx\n",
     tctimes, _LL_CAST_ stat->czxid, tmtimes,
    _LL_CAST_ stat->mzxid,
    (unsigned int)stat->version, (unsigned int)stat->aversion,
    _LL_CAST_ stat->ephemeralOwner);
}

void ebbrt::ZooKeeper::my_string_completion(int rc, const char *name, const void *data) {
    fprintf(stderr, "[%s]: rc = %d\n", (char*)(data==0?"null":data), rc);
    if (!rc) {
        fprintf(stderr, "\tname = %s\n", name);
    }
}

void ebbrt::ZooKeeper::my_string_completion_free_data(int rc, const char *name, const void *data) {
    my_string_completion(rc, name, data);
    free((void*)data);
}

void ebbrt::ZooKeeper::my_data_completion(int rc, const char *value, int value_len,
        const ZkStat *stat, const void *data) {
    struct timeval tv;
    int sec;
    int usec;
    gettimeofday(&tv, 0);
    sec = tv.tv_sec - startTime.tv_sec;
    usec = tv.tv_usec - startTime.tv_usec;
    fprintf(stderr, "time = %d msec\n", sec*1000 + usec/1000);
   fprintf(stderr, "%s: rc = %d\n", (char*)data, rc);
    if (value) {
        fprintf(stderr, " value_len = %d\n", value_len);
        assert(write(2, value, value_len) == value_len);
    }
    fprintf(stderr, "\nstat:\n");
      dumpstat(stat);
    free((void*)data);
}

void ebbrt::ZooKeeper::my_silent_data_completion(int rc, const char *value, int value_len,
        const ZkStat *stat, const void *data) {
    fprintf(stderr, "data completion %s rc = %d\n",(char*)data,rc);
    free((void*)data);
}

void ebbrt::ZooKeeper::my_strings_completion(int rc, const struct String_vector *strings,
        const void *data) {
    struct timeval tv;
    int sec;
    int usec;
    int i;

    gettimeofday(&tv, 0);
    sec = tv.tv_sec - startTime.tv_sec;
    usec = tv.tv_usec - startTime.tv_usec;
    fprintf(stderr, "time = %d msec\n", sec*1000 + usec/1000);
    fprintf(stderr, "%s: rc = %d\n", (char*)data, rc);
    if (strings)
        for (i=0; i < strings->count; i++) {
            fprintf(stderr, "\t%s\n", strings->data[i]);
        }
    free((void*)data);
    gettimeofday(&tv, 0);
    sec = tv.tv_sec - startTime.tv_sec;
    usec = tv.tv_usec - startTime.tv_usec;
    fprintf(stderr, "time = %d msec\n", sec*1000 + usec/1000);
}

void ebbrt::ZooKeeper::my_strings_stat_completion(int rc, const struct String_vector *strings,
        const ZkStat *stat, const void *data) {
    my_strings_completion(rc, strings, data);
    dumpstat(stat);
}

void ebbrt::ZooKeeper::my_void_completion(int rc, const void *data) {
    fprintf(stderr, "%s: rc = %d\n", (char*)data, rc);
    free((void*)data);
}

void ebbrt::ZooKeeper::my_stat_completion(int rc, const ZkStat *stat, const void *data) {
    fprintf(stderr, "%s: rc = %d stat:\n", (char*)data, rc);
    dumpstat(stat);
    free((void*)data);
}

void ebbrt::ZooKeeper::my_silent_stat_completion(int rc, const ZkStat *stat,
        const void *data) {
    //    fprintf(stderr, "state completion: [%s] rc = %d\n", (char*)data, rc);
    free((void*)data);
}

int ebbrt::ZooKeeper::startsWith(const char *line, const char *prefix) {
    int len = strlen(prefix);
    return strncmp(line, prefix, len) == 0;
}


void ebbrt::ZooKeeper::Input(char *line) {
    int rc;
    int async = ((line[0] == 'a') && !(startsWith(line, "addauth ")));
    if (async) {
        line++;
    }
    if (startsWith(line, "help")) {
      fprintf(stderr, "    create [+[e|s]] <path>\n");
      fprintf(stderr, "    delete <path>\n");
      fprintf(stderr, "    set <path> <data>\n");
      fprintf(stderr, "    get <path>\n");
      fprintf(stderr, "    ls <path>\n");
      fprintf(stderr, "    ls2 <path>\n");
      fprintf(stderr, "    sync <path>\n");
      fprintf(stderr, "    exists <path>\n");
      fprintf(stderr, "    wexists <path>\n");
      fprintf(stderr, "    myid\n");
      fprintf(stderr, "    verbose\n");
      fprintf(stderr, "    addauth <id> <scheme>\n");
      fprintf(stderr, "    quit\n");
      fprintf(stderr, "\n");
      fprintf(stderr, "    prefix the command with the character 'a' to run the command asynchronously.\n");
      fprintf(stderr, "    run the 'verbose' command to toggle verbose logging.\n");
      fprintf(stderr, "    i.e. 'aget /foo' to get /foo asynchronously\n");
    } else if (startsWith(line, "verbose")) {
      if (verbose) {
        verbose = 0;
        zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
        fprintf(stderr, "logging level set to WARN\n");
      } else {
        verbose = 1;
        zoo_set_debug_level(ZOO_LOG_LEVEL_DEBUG);
        fprintf(stderr, "logging level set to DEBUG\n");
      }
    } else if (startsWith(line, "get ")) {
        line += 4;
        if (line[0] != '/') {
            fprintf(stderr, "Path must start with /, found: %s\n", line);
            return;
        }
               
        rc = zoo_aget(zk_, line, 1, my_data_completion, strdup(line));
        if (rc) {
            fprintf(stderr, "Error %d for %s\n", rc, line);
        }
    } else if (startsWith(line, "set ")) {
        char *ptr;
        line += 4;
        if (line[0] != '/') {
            fprintf(stderr, "Path must start with /, found: %s\n", line);
            return;
        }
        ptr = strchr(line, ' ');
        if (!ptr) {
            fprintf(stderr, "No data found after path\n");
            return;
        }
        *ptr = '\0';
        ptr++;
        if (async) {
            rc = zoo_aset(zk_, line, ptr, strlen(ptr), -1, my_stat_completion,
                    strdup(line));
        } else {
            ZkStat stat;
            rc = zoo_set2(zk_, line, ptr, strlen(ptr), -1, &stat);
        }
        if (rc) {
            fprintf(stderr, "Error %d for %s\n", rc, line);
        }
    } else if (startsWith(line, "ls ")) {
        line += 3;
        if (line[0] != '/') {
            fprintf(stderr, "Path must start with /, found: %s\n", line);
            return;
        }
        gettimeofday(&startTime, 0);
        rc= zoo_aget_children(zk_, line, 1, my_strings_completion, strdup(line));
        if (rc) {
            fprintf(stderr, "Error %d for %s\n", rc, line);
        }
    } else if (startsWith(line, "ls2 ")) {
        line += 4;
        if (line[0] != '/') {
            fprintf(stderr, "Path must start with /, found: %s\n", line);
            return;
        }
        gettimeofday(&startTime, 0);
        rc= zoo_aget_children2(zk_, line, 1, my_strings_stat_completion, strdup(line));
        if (rc) {
            fprintf(stderr, "Error %d for %s\n", rc, line);
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
            fprintf(stderr, "Path must start with /, found: %s\n", line);
            return;
        }
        fprintf(stderr, "Creating [%s] node\n", line);
//        {
//            struct ACL _CREATE_ONLY_ACL_ACL[] = {{ZOO_PERM_CREATE, ZOO_ANYONE_ID_UNSAFE}};
//            struct ACL_vector CREATE_ONLY_ACL = {1,_CREATE_ONLY_ACL_ACL};
//            rc = zoo_acreate(zk_, line, "new", 3, &CREATE_ONLY_ACL, flags,
//                    my_string_completion, strdup(line));
//        }
        rc = zoo_acreate(zk_, line, "new", 3, &ZOO_OPEN_ACL_UNSAFE, flags,
                my_string_completion_free_data, strdup(line));
        if (rc) {
            fprintf(stderr, "Error %d for %s\n", rc, line);
        }
    } else if (startsWith(line, "delete ")) {
        line += 7;
        if (line[0] != '/') {
            fprintf(stderr, "Path must start with /, found: %s\n", line);
            return;
        }
        if (async) {
            rc = zoo_adelete(zk_, line, -1, my_void_completion, strdup(line));
        } else {
            rc = zoo_delete(zk_, line, -1);
        }
        if (rc) {
            fprintf(stderr, "Error %d for %s\n", rc, line);
        }
    } else if (startsWith(line, "sync ")) {
        line += 5;
        if (line[0] != '/') {
            fprintf(stderr, "Path must start with /, found: %s\n", line);
            return;
        }
        rc = zoo_async(zk_, line, my_string_completion_free_data, strdup(line));
        if (rc) {
            fprintf(stderr, "Error %d for %s\n", rc, line);
        }
    } else if (startsWith(line, "wexists ")) {
#ifdef THREADED
        ZkStat stat;
#endif
        line += 8;
        if (line[0] != '/') {
            fprintf(stderr, "Path must start with /, found: %s\n", line);
            return;
        }
#ifndef THREADED
        rc = zoo_awexists(zk_, line, CallWatcher, (void*) 0, my_stat_completion, strdup(line));
#else
        rc = zoo_wexists(zk_, line, CallWatcher, (void*) 0, &stat);
#endif
        if (rc) {
            fprintf(stderr, "Error %d for %s\n", rc, line);
        }
    } else if (startsWith(line, "exists ")) {
#ifdef THREADED
        ZkStat stat;
#endif
        line += 7;
        if (line[0] != '/') {
            fprintf(stderr, "Path must start with /, found: %s\n", line);
            return;
        }
#ifndef THREADED
        rc = zoo_aexists(zk_, line, 1, my_stat_completion, strdup(line));
#else
        rc = zoo_exists(zk_, line, 1, &stat);
#endif
        if (rc) {
            fprintf(stderr, "Error %d for %s\n", rc, line);
        }
    } else if (strcmp(line, "myid") == 0) {
        printf("session Id = %llx\n", _LL_CAST_ zoo_client_id(zk_)->client_id);
    } else if (strcmp(line, "reinit") == 0) {
        zookeeper_close(zk_);
        // we can't send myid to the server here -- zookeeper_close() removes 
        // the session on the server. We must start anew.
        zk_ = zookeeper_init(hostPort, CallWatcher, 30000, 0, 0, 0);
    } else if (startsWith(line, "quit")) {
        fprintf(stderr, "Quitting...\n");
    } else if (startsWith(line, "addauth ")) {
      char *ptr;
      line += 8;
      ptr = strchr(line, ' ');
      if (ptr) {
        *ptr = '\0';
        ptr++;
      }
      zoo_add_auth(zk_, line, ptr, ptr ? strlen(ptr) : 0, NULL, NULL);
    }
}


/*
void *do_io(void *v)
{
    zhandle_t *zh = (zhandle_t*)v;
    struct pollfd fds[2];
    struct adaptor_threads *adaptor_threads = zh->adaptor_priv;

    api_prolog(zh);
    notify_thread_ready(zh);
    LOG_DEBUG(("started IO thread"));
    fds[0].fd=adaptor_threads->self_pipe[0];
    fds[0].events=POLLIN;
    while(!zh->close_requested) {
        struct timeval tv;
        int fd;
        int interest;
        int timeout;
        int maxfd=1;
        int rc;
        
        zookeeper_interest(zh, &fd, &interest, &tv);
        if (fd != -1) {
            fds[1].fd=fd;
            fds[1].events=(interest&ZOOKEEPER_READ)?POLLIN:0;
            fds[1].events|=(interest&ZOOKEEPER_WRITE)?POLLOUT:0;
            maxfd=2;
        }
        timeout=tv.tv_sec * 1000 + (tv.tv_usec/1000);
        
        poll(fds,maxfd,timeout);
        if (fd != -1) {
            interest=(fds[1].revents&POLLIN)?ZOOKEEPER_READ:0;
            interest|=((fds[1].revents&POLLOUT)||(fds[1].revents&POLLHUP))?ZOOKEEPER_WRITE:0;
        }
        if(fds[0].revents&POLLIN){
            // flush the pipe
            char b[128];
            while(read(adaptor_threads->self_pipe[0],b,sizeof(b))==sizeof(b)){}
        }        
        // dispatch zookeeper events
        rc = zookeeper_process(zh, interest);
        // check the current state of the zhandle and terminate 
        // if it is_unrecoverable()
        if(is_unrecoverable(zh))
            break;
    }
    api_epilog(zh, 0);    
    LOG_DEBUG(("IO thread terminated"));
    return 0;
}


*/
