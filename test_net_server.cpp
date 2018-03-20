#include <stdio.h>
#include <unistd.h>

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/fibers/Baton.h>

#include "follib.h"

using namespace folly;
using namespace folly::fibers;


class FollibReadCBs : public folly::AsyncReader::ReadCallback {
public:
   explicit FollibReadCBs(void *ptr,
                          size_t l) : buf(ptr), bufSize(l) { }

   ~FollibReadCBs() override { }
   void getReadBuffer(void **bufPtr, size_t *lenPtr) override {
      *bufPtr = buf;
      *lenPtr = bufSize;
      printf("-- %s:%u\n", __func__, __LINE__);
   }
   void readDataAvailable(size_t readLen) noexcept override {
      fiber_mgr *mgr = follib_get_mgr();
      printf("-- %s:%u\n", __func__, __LINE__);
      len = readLen;
      buf = nullptr;
      if (mgr->readCB) {
         mgr->baton.post();
      }
      printf("-- %s:%u\n", __func__, __LINE__);
   }
   void readEOF() noexcept override {
      fiber_mgr *mgr = follib_get_mgr();
      printf("-- %s:%u\n", __func__, __LINE__);
      if (mgr->readCB) {
         mgr->baton.post();
      }
      printf("-- %s:%u\n", __func__, __LINE__);
   }
   void readErr(const AsyncSocketException& except) noexcept override {
      fiber_mgr *mgr = follib_get_mgr();
      printf("-- %s:%u\n", __func__, __LINE__);
      len = -1;
      if (mgr->readCB) {
         mgr->baton.post();
      }
      printf("-- %s:%u\n", __func__, __LINE__);
   }

   ssize_t   len{0};
   void     *buf{nullptr};
   size_t    bufSize{0};
};


class FollibServerAcceptCBs : public AsyncServerSocket::AcceptCallback {
public:
   FollibServerAcceptCBs() { }

   void setConnectionAcceptedFn(const std::function<void(int, const SocketAddress&)>& fn) {
      connectionAcceptedFn_ = fn;
   }
   void setAcceptErrorFn(const std::function<void(const std::exception&)>& fn) {
      acceptErrorFn_ = fn;
   }
   void setAcceptStartedFn(const std::function<void()>& fn) {
      acceptStartedFn_ = fn;
   }
   void setAcceptStoppedFn(const std::function<void()>& fn) {
      acceptStoppedFn_ = fn;
   }

   void connectionAccepted(int fd,
                           const SocketAddress& addr) noexcept override {
      if (connectionAcceptedFn_) {
         connectionAcceptedFn_(fd, addr);
      }
   }
   void acceptError(const std::exception& ex) noexcept override {
      if (acceptErrorFn_) {
         acceptErrorFn_(ex);
      }
   }
   void acceptStarted() noexcept override {
      if (acceptStartedFn_) {
         acceptStartedFn_();
      }
   }
   void acceptStopped() noexcept override {
      if (acceptStoppedFn_) {
         acceptStoppedFn_();
      }
   }

private:
   std::function<void(int, const folly::SocketAddress&)> connectionAcceptedFn_;
   std::function<void(const std::exception&)> acceptErrorFn_;
   std::function<void()> acceptStartedFn_;
   std::function<void()> acceptStoppedFn_;
};


class TestConn {
public:
   TestConn() {
      printf("-- %s:%u\n", __func__, __LINE__);
   }
   ~TestConn() {
      printf("-- %s:%u\n", __func__, __LINE__);
   }
   std::shared_ptr<AsyncSocket> sock;
};

class FollibServerSocket {
public:
   std::shared_ptr<AsyncServerSocket>  sock;
   bool                                done{false};
};

struct TestServer {
   ~TestServer() {
      printf("-- %s:%u\n", __func__, __LINE__);
   }
   SocketAddress                       addr;
   std::shared_ptr<AsyncServerSocket>  sock;
   std::vector<TestConn *>             conns;
   bool                                done{false};
};




int
follib_net_accept(std::shared_ptr<AsyncServerSocket> sock)
{
   FollibServerAcceptCBs acceptCBs;
   fiber_mgr *mgr = follib_get_mgr();
   int fd = -1;

   auto acceptFunc = [&](int fdIn, const SocketAddress& addr) {
      printf("accept: fd=%d\n", fdIn);

      fd = fdIn;
      mgr->baton.post();

      printf("-- conn_accepted %s:%u\n", __func__, __LINE__);
   };

   auto acceptErrFunc = [=](const std::exception& ex) {
      printf("-- accept error: %s\n", ex.what());
      mgr->baton.post();
   };

   auto acceptStoppedFunc = [=]() {
      printf("-- accept_stopped %s:%u\n", __func__, __LINE__);
//      mgr->baton.post();
   };

   acceptCBs.setConnectionAcceptedFn(acceptFunc);
   acceptCBs.setAcceptErrorFn(acceptErrFunc);
   acceptCBs.setAcceptStoppedFn(acceptStoppedFunc);
//   acceptCBs.setAcceptStartedFn([&]{ printf("accept started.\n"); });

   sock->addAcceptCallback(&acceptCBs, &mgr->evb);
   sock->startAccepting();

   printf("-- %s:%u\n", __func__, __LINE__);
   mgr->baton.wait();
   printf("-- %s:%u\n", __func__, __LINE__);

   sock->stopAccepting();
//   sock->removeAcceptCallback(&acceptCBs, &mgr->evb);

   mgr->baton.reset();

   printf("%s got fd=%d\n", __func__, fd);

   return fd;
}


int
follib_net_close(std::shared_ptr<AsyncServerSocket> sock)
{
   fiber_mgr *mgr = follib_get_mgr();

   printf("-- %s:%u\n", __func__, __LINE__);
   if (sock->getAccepting()) {
      mgr->baton.post();
   }
   sock.reset();
   printf("-- %s:%u\n", __func__, __LINE__);

   return 0;
}


/*
 * follib_net_read --
 *
 *      Socket read.
 */
ssize_t
follib_net_read(std::shared_ptr<AsyncSocket> sock,
                void                        *buf,
                size_t                       len)
{
   fiber_mgr *mgr = follib_get_mgr();
   FollibReadCBs readCB(buf, len);

   mgr->readCB = &readCB;

   sock->setReadCB(&readCB);

   FLOG(0, "mgr %u: %s: len: %zu\n", mgr->idx, __func__, len);

   mgr->baton.wait();
   sock->setReadCB(nullptr);

   mgr->readCB = nullptr;

   return readCB.len;
}



static void
run_accept_fiber(void *clientData)
{
   TestServer *server = (TestServer *)clientData;

   printf("-- %s:%u\n", __func__, __LINE__);

   server->sock = AsyncServerSocket::newSocket(follib_get_evb());

   server->sock->setReusePortEnabled(true);
   server->sock->bind(server->addr);
   server->sock->listen(10);

   while (!server->done) {
      int fd = follib_net_accept(server->sock);
      if (fd < 0) {
         printf("-- %s:%u\n", __func__, __LINE__);
         break;
      }
      printf("accept got fd=%d\n", fd);
      close(fd);
   }

   printf("-- %s:%u\n", __func__, __LINE__);
}

static void
test_net_create_server(TestServer& server,
                       uint32_t    thId)
{
   server.addr = SocketAddress("127.0.0.1", 1666);

   printf("creating server at %s:%u\n",
          server.addr.getAddressStr().c_str(),
          server.addr.getPort());

   auto fibMgr = follib_get_manager(thId);
   fibMgr->addTaskRemote([&server]() { run_accept_fiber(&server); });

   printf("-- %s:%u\n", __func__, __LINE__);
}


static void
test_net_delete_server(TestServer& server,
                       uint32_t    thId)
{
   auto evb = follib_get_evb(thId);

   printf("-- %s:%u\n", __func__, __LINE__);

   server.done = true;
   auto deleteServerFunc = [&] {
      printf("-- %s:%u\n", __func__, __LINE__);
      follib_net_close(server.sock);
      printf("-- %s:%u\n", __func__, __LINE__);
   };

   evb->runInEventBaseThreadAndWait(deleteServerFunc);
   printf("-- %s:%u\n", __func__, __LINE__);
}


void
test_net_server()
{
   printf("----- %s -----\n", __func__);

   follib_init();

   {
      TestServer srvr;

      test_net_create_server(srvr, 0);
      printf("sleeping 15 sec.\n");
      sleep(15);
      test_net_delete_server(srvr, 0);
   }

   follib_exit();
}
