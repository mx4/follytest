#include <stdio.h>
#include <unistd.h>

#include <map>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManager.h>

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/experimental/io/AsyncIO.h>

#include "follib.h"
#include "test_net_server.h"

using namespace folly;

typedef void* (FiberRunFunc)(void*);

class Fib {
public:
   void Wait() {
      printf("-- %s:%u\n", __func__, __LINE__);

      if (folly::fibers::onFiber()) {
         baton_.wait();
      } else {
         while (!baton_.try_wait()) {
            follib_run_loop_once(); // XXX
         }
      }
   }
   void Complete(void *res) {
      printf("-- %s:%u\n", __func__, __LINE__);
      result_ = res;
      baton_.post();
   }
   void *GetResult() { return result_; }

private:
   folly::fibers::Baton baton_;
   void                *result_{nullptr};
};


class TestNetReadCB : public folly::AsyncReader::ReadCallback {
public:
   TestNetReadCB(void *buf, uint64_t len) {
      ioBuf_ = IOBuf::takeOwnership(buf, len, (uint64_t)0,
                                    [](void *buf, void *user) { /* wheee */ });
      DCHECK_EQ(ioBuf_->writableData(), buf);
      DCHECK_EQ(ioBuf_->tailroom(), len);
   }
   void getReadBuffer(void  **bufPtr,
                      size_t *lenPtr) override {
      *bufPtr = ioBuf_->writableData();
      *lenPtr = ioBuf_->tailroom();
      FLOG(1, "-- %s:%u buf=0x%p len=%zu\n",
           __func__, __LINE__, *bufPtr, *lenPtr);
   }
   void readDataAvailable(size_t readLen) noexcept override {
      ioBuf_->append(readLen);
      FLOG(1, " tailroom / length: %zd / %zd\n",
           ioBuf_->tailroom(), ioBuf_->length());
      if (ioBuf_->tailroom() == 0) {
         Signal();
      }
   }
   void readEOF() noexcept override {
//      printf("-- %s\n", __func__);
      eof_ = true;
      Signal();
   }
   void readErr(const AsyncSocketException& ex) noexcept override {
      FLOG(3, "-- %s: %s\n", __func__, ex.what());
//      exc_ = ex;
      Signal();
      follib_stop_test();
   }

   void ReadAllData() {
      baton_.wait();
      baton_.reset();
   }
   size_t ReadLen() const { return ioBuf_->length(); }
   bool   IsEOF() const { return eof_; }
   void   Signal() { baton_.post(); }

   bool                   closed_{false};

private:
   std::unique_ptr<IOBuf> ioBuf_;
   folly::fibers::Baton   baton_;
   bool                   eof_{false};
};


class TestNetConn {
public:
   TestNetConn(int fd) : fd_(fd) {}
   ~TestNetConn() { }

   void Start();
   void Stop();
   int  GetFd() const { return fd_; }
   void Close();
   void DoWork();

private:
   std::shared_ptr<AsyncSocket>  sock_;
   int                           fd_{-1};
   Fib                          *fib_{nullptr};
};



class TestNetServer {
public:
   TestNetServer();
   ~TestNetServer();
   int StartAccept(const char *addrStr, uint16_t port, uint32_t threadId);
   void Exit();
   void AcceptLoop();

private:
   int InitOnEventBase(std::string addr, uint16_t port);
   void ExitOnEventBase();

   Fib                                        *acceptFib_{nullptr};
   std::shared_ptr<AsyncServerSocket>          acceptSock_;
   std::map<int, std::shared_ptr<TestNetConn>> connMap_;
   folly::SharedMutex                          mutex_;
};



class TestNetAcceptCB : public AsyncServerSocket::AcceptCallback {
public:
   void connectionAccepted(int fd,
                           const SocketAddress& addr) noexcept override {
      FLOG(0, "-- %s:%u fd=%d\n", __func__, __LINE__, fd);
      fd_ = fd;
      baton_.post();
   }
   void acceptError(const std::exception& ex) noexcept override {
      FLOG(0, "-- %s:%u '%s'\n", __func__, __LINE__, ex.what());
   }
   void acceptStarted() noexcept override {
      refCount_++;
      FLOG(0, "-- %s:%u refCount=%d\n", __func__, __LINE__, refCount_);
   }
   void acceptStopped() noexcept override {
      baton_.post();
      refCount_--;
      FLOG(0, "-- %s:%u refCount=%d\n", __func__, __LINE__, refCount_);
      if (refCount_ == 0) {
         delete this;
      }
   }
   int getFd() const { return fd_; }
   void Wait() { baton_.wait(); }
   void Reset() { baton_.reset(); fd_ = -1; }

   int                  refCount_{0};

private:
   folly::fibers::Baton baton_;
   int                  fd_{-1};
};


class TestSock {
public:

   ssize_t Read(void *buf, size_t bufLen);

private:
   TestNetReadCB                readCB_;
   std::shared_ptr<AsyncSocket> sock_;
};


Fib *
Fiber_Create(FiberRunFunc *func,
             void         *param)
{
   auto mgr = follib_get_manager();
   Fib *fib;

   fib = new(std::nothrow) Fib();

   FLOG(1, "-- %s:%u func=%p param=%p\n", __func__, __LINE__,
        (void *)func, param);

   auto fn = [fib, func, param]() {
      void *res = func(param);
      fib->Complete(res);
   };

   mgr->addTask(std::move(fn));

   return fib;
}


int
Fiber_Join(Fib   *fib,
           void **result)
{
   printf("-- %s:%u\n", __func__, __LINE__);
   fib->Wait();
   printf("-- %s:%u\n", __func__, __LINE__);

   if (result) {
      *result = fib->GetResult();
   }
   delete fib;
   return 0;
}


int
Fiber_Accept(std::shared_ptr<AsyncServerSocket> sock)
{
   TestNetAcceptCB *acceptObj;
   auto evb = follib_get_evb();

   acceptObj = new TestNetAcceptCB();

   sock->startAccepting();
   sock->addAcceptCallback(acceptObj, evb);
   acceptObj->refCount_++;

   acceptObj->Wait();

   auto fd = acceptObj->getFd();
   if (sock->getAccepting()) {
      sock->removeAcceptCallback(acceptObj, evb);
   }

   acceptObj->refCount_--;
   if (acceptObj->refCount_ == 0) {
      delete acceptObj;
   }

   return fd;
}


int
Fiber_Close(std::shared_ptr<AsyncServerSocket> sock)
{
   if (sock->getAccepting()) {
      sock->stopAccepting();
   }
   sock.reset();

   return 0;
}


ssize_t
Follib_Read(std::shared_ptr<AsyncSocket> sock,
            void                        *buf,
            size_t                       len)
{
   auto readCB = new TestNetReadCB(buf, len);
   ssize_t res;

   sock->setReadCB(readCB);

   readCB->ReadAllData();

   sock->setReadCB(nullptr);

   res = -1;
   if (!readCB->IsEOF() && !readCB->closed_) {
      res = readCB->ReadLen();
   }

   delete readCB;

   FLOG(2, "Just read %zd bytes.\n", res);

   return res;
}


void
TestNetConn::DoWork()
{
   printf("-- %s:%u conn work starting\n", __func__, __LINE__);

   while (true) {
      char buf[33];
      ssize_t res;

      buf[32] = '\0';
      res = Follib_Read(sock_, buf, sizeof buf - 1);
      if (res < 0) {
         printf("-- %s:%u\n", __func__, __LINE__);
         break;
      }

      FLOG(1, "Read %zd bytes.\n", res);
      std::string s(&buf[0]);
      const auto isEOLFunc = [](char x){ return x == '\n' || x == '\r'; };
      s.erase(std::remove_if(s.begin(), s.end(), isEOLFunc), s.end());

      printf("-- '%s'\n", s.c_str());
   }

   printf("-- %s:%u conn work done\n", __func__, __LINE__);
}


void
TestNetConn::Close()
{
   if (auto readCB = sock_->getReadCallback()) {
      TestNetReadCB *cb = dynamic_cast<TestNetReadCB *>(readCB);

      printf("%s: signalling end of read.\n", __func__);
      cb->closed_ = true;
      cb->Signal();
   }

   sock_.reset();
}


void *
TestNetConnReadWrapper(void *clientData)
{
   TestNetConn *conn = static_cast<TestNetConn *>(clientData);

   conn->DoWork();

   return nullptr;
}


void
TestNetConn::Start()
{
   fib_ = Fiber_Create(TestNetConnReadWrapper, this);

   printf("%u: initing  connection w/ fd=%d\n", follib_get_mgr_idx(), fd_);

   sock_ = AsyncSocket::newSocket(follib_get_evb(), fd_);

   /*
    * We only want get read callbacks to read a certain number of bytes. Once
    * we've read all the data needed we unregister those callbacks. If
    * maxReadsPerEvent is >1, we may get getReadBuffer callbacks even though
    * we're done reading. We wouldn't know what to do with the data just read.
    */
   sock_->setMaxReadsPerEvent(1);

}


void
TestNetConn::Stop()
{
   printf("-- %s:%u stopping connection\n", __func__, __LINE__);
   Close();
   if (fib_) {
      printf("-- %s:%u\n", __func__, __LINE__);
      Fiber_Join(fib_, nullptr);
   }
}


void
TestNetServer::AcceptLoop()
{
   printf("-- %s:%u -- accept loop started\n", __func__, __LINE__);
   while (true) {
      printf("calling accept..\n");
      int fd = Fiber_Accept(acceptSock_);
      printf("accept returns fd=%d\n", fd);
      if (fd < 0) {
         break;
      }
      auto conn = std::make_shared<TestNetConn>(fd);

      connMap_[fd] = conn;
      conn->Start();
   }

   printf("-- %s:%u -- accept loop done\n", __func__, __LINE__);
}


static void *
AcceptWrapperFunc(void *param)
{
   TestNetServer *server = static_cast<TestNetServer *>(param);

   printf("thread %u -- %s:%u\n", follib_get_mgr_idx(), __func__, __LINE__);

   server->AcceptLoop();

   printf("thread %u -- %s:%u\n", follib_get_mgr_idx(), __func__, __LINE__);

   return nullptr;
}


int
TestNetServer::InitOnEventBase(std::string addrStr,
                               uint16_t    port)
{
   auto addr = SocketAddress(addrStr, port);
   auto evb = follib_get_evb();

   acceptSock_ = AsyncServerSocket::newSocket(evb);

   acceptSock_->setReusePortEnabled(true);
   acceptSock_->bind(addr);
   acceptSock_->listen(10);

   acceptFib_ = Fiber_Create(AcceptWrapperFunc, this);

   return 0;
}


int
TestNetServer::StartAccept(const char *addrStr,
                           uint16_t    port,
                           uint32_t    thId)
{
   auto evb = follib_get_evb(thId);
   int err = 0;

   printf("%s: init server at %s:%u\n", __func__, addrStr, port);

   folly::fibers::Baton baton;
   evb->runInEventBaseThread([&]() {
      err = InitOnEventBase(addrStr, port);
      baton.post();
   });

   baton.wait();

   return err;
}

TestNetServer::TestNetServer()
{
   LOG(INFO) << __func__;
}

TestNetServer::~TestNetServer()
{
   LOG(INFO) << __func__;
}


void
TestNetServer::ExitOnEventBase()
{
   Fiber_Close(acceptSock_);
   if (acceptFib_) {
      Fiber_Join(acceptFib_, nullptr);
   }

   for (auto p : connMap_) {
      auto conn = p.second;
      FLOG(0, "Closing conn for fd=%d\n", conn->GetFd());

      conn->Stop();
   }

   acceptSock_.reset();
}


void
TestNetServer::Exit()
{
   printf("stopping accept server\n");

   auto evb = acceptSock_->getEventBase();
   folly::fibers::Baton baton;

   evb->runInEventBaseThread([&]() {
      ExitOnEventBase();
      baton.post();
   });

   baton.wait();

   printf("server accept stopped\n");
}


void
test_net_server()
{
   printf("----- %s -----\n", __func__);

   follib_init();

   {
      auto server = std::make_shared<TestNetServer>();

      auto res = server->StartAccept("127.0.0.1", 1666, 1);
      if (res != 0) {
         goto done;
      }

      printf("Waiting for ctr-c.\n");
      follib_run_loop(false);
      printf("Got ctrl-c.\n");
done:
      server->Exit();

      follib_run_loop_until_no_ready();
   }

   follib_exit();
}
