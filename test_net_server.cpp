#include <stdio.h>
#include <unistd.h>

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/fibers/Baton.h>

#include "follib.h"

using namespace folly;
using namespace folly::fibers;


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

class FollibReadCBs : public folly::AsyncReader::ReadCallback {
public:
   explicit FollibReadCBs(size_t maxBufSize = 4 * 4096) : bufSize(maxBufSize) { }

   ~FollibReadCBs() override {
      for (auto b : buffers) {
         free(b);
      }
      free(buf);
   }
   void getReadBuffer(void **bufPtr, size_t *lenPtr) override {
      if (buf) {
         *bufPtr = buf;
         *lenPtr = bufSize;
         return;
      }
      buf = (uint8_t *)malloc(bufSize);
      *bufPtr = buf;
      *lenPtr = bufSize;
      printf("-- %s\n", __func__);
   }
   void readDataAvailable(size_t readLen) noexcept override {
      printf("-- %s\n", __func__);
      len = readLen;
      buffers.push_back(buf);
      buf = nullptr;
      len = 0;
   }
   void readEOF() noexcept override {
      printf("-- %s\n", __func__);
   }
   void readErr(const AsyncSocketException& ex) noexcept override {
      printf("-- %s: %s\n", __func__, ex.what());
   }

   size_t                 len{0};
   size_t                 bufSize{0};
   uint8_t               *buf{nullptr};
   std::vector<uint8_t *> buffers;
};


struct TestConn {
   ~TestConn() {
      printf("-- %s:%u\n", __func__, __LINE__);
      printf("-- %s:%u\n", __func__, __LINE__);
   }
   std::shared_ptr<AsyncSocket> sock;
   FollibReadCBs                readCBs;
};

struct TestServer {
   ~TestServer() {
      printf("-- %s:%u\n", __func__, __LINE__);
   }
   FollibServerAcceptCBs               acceptCBs;
   SocketAddress                       addr;
   std::shared_ptr<AsyncServerSocket>  sock;
   std::vector<TestConn *>             conns;
};


static void
test_net_create_server(TestServer& server,
                       uint32_t    thId)
{
   auto evb = follib_get_evb(thId);

   printf("evb: %p\n", (void *)evb);

   server.addr = SocketAddress("127.0.0.1", 1666);

   printf("creating server at %s:%u\n",
          server.addr.getAddressStr().c_str(),
          server.addr.getPort());

   server.acceptCBs.setConnectionAcceptedFn([&server, evb](int fd, const SocketAddress& addr) {
      TestConn *conn = (TestConn *)calloc(1, sizeof *conn);

      printf("accept: fd=%d\n", fd);
      conn->sock = AsyncSocket::newSocket(evb, fd);
      conn->sock->setNoDelay(true);
      conn->sock->setReadCB(&conn->readCBs);
      server.conns.push_back(conn);
   });
   server.acceptCBs.setAcceptErrorFn([&](const std::exception& ex) {
      printf("accept error: %s\n", ex.what());
   });
   server.acceptCBs.setAcceptStartedFn([&]{ printf("accept started.\n"); });
   server.acceptCBs.setAcceptStoppedFn([&]{ printf("accept stopped.\n"); });

   auto createServerFunc = [&] {
      printf("-- %s:%u\n", __func__, __LINE__);
      server.sock = AsyncServerSocket::newSocket(evb);

      server.sock->setReusePortEnabled(true);
      server.sock->addAcceptCallback(&server.acceptCBs, evb);
      server.sock->bind(server.addr);
      server.sock->listen(10);
      server.sock->startAccepting();
      printf("-- %s:%u\n", __func__, __LINE__);
   };

   evb->runInEventBaseThreadAndWait(createServerFunc);
   printf("-- %s:%u\n", __func__, __LINE__);
}


static void
test_net_delete_server(TestServer& server,
                       uint32_t    thId)
{
   auto evb = follib_get_evb(thId);

   printf("-- %s:%u\n", __func__, __LINE__);

   auto deleteServerFunc = [&] {
      printf("-- %s:%u\n", __func__, __LINE__);
      server.sock->stopAccepting();
      server.sock.reset();
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
