#include <stdio.h>
#include <map>

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/SharedMutex.h>

#include "follib.h"

#include "test_server.h"

using namespace folly;

static const uint64_t readLen = 64;

class TestConn;

/*
 * TestServer represents a network server implementation. This could be our 9p
 * server.
 */
class TestServer : public AsyncServerSocket::AcceptCallback,
                   public enable_shared_from_this<TestServer> {
public:
   TestServer() {}
   ~TestServer();
   std::shared_ptr<TestServer> GetSharedPtr() { return shared_from_this(); }
   int  StartAccept(const char *addrStr, uint16_t port, uint32_t threadId);
   void Exit();
   void RemoveConn(std::shared_ptr<TestConn> conn);

   void connectionAccepted(int fd, const SocketAddress& addr) noexcept override {
      // here this runs on the event-base handling the server socket.
      AddConn(fd);
   }
   void acceptError(const std::exception& ex) noexcept override {
      printf("server accept error: %s\n", ex.what());
      follib_stop_test();
   }
   void acceptStarted() noexcept override {
      printf("now accepting incoming connections on fd=%d\n",
             acceptSock_->getSocket());
   }
   void acceptStopped() noexcept override {
      printf("stopped accepting connections.\n");
      // can't use acceptSock_ here. It may already be gone.
   }

   uint32_t GetConnThreadId() {
      auto res = thId_;
      assert(thId_ < follib_get_num_managers());
      thId_ = (thId_ + 1) % follib_get_num_managers();
      return res;
   }

private:
   void StopAccept();
   void AddConn(int fd);
   int  InitOnEventBase(const std::string& addrStr, uint16_t port);
   void ExitOnEventBase();
   void StopConn(std::shared_ptr<TestConn> conn);

   std::shared_ptr<AsyncServerSocket>         acceptSock_;
   std::map<int, std::shared_ptr<TestConn>>   connMap_;
   folly::SharedMutex                         mutex_;
   uint32_t                                   thId_{2};
};


class TestConn : public folly::AsyncReader::ReadCallback,
                 public enable_shared_from_this<TestConn> {
public:
   static std::shared_ptr<TestConn> newConn(std::shared_ptr<TestServer> server,
                                            int fd);

   std::shared_ptr<TestConn> GetSharedPtr() { return shared_from_this(); }
   TestConn(std::shared_ptr<TestServer> srv) : server_(srv) { }
   ~TestConn();
   int GetFd() const { return sock_->getFd(); }
   folly::EventBase *GetEventBase() const { return sock_->getEventBase(); }

   void StopWork() {
      exit_ = true;
      baton_.post();
   }

   void getReadBuffer(void **bufPtr,
                      size_t *lenPtr) override {
      if (!readBuf_) {
         readBuf_ = IOBuf::create(readLen);
      }
      *bufPtr = readBuf_->writableData();
      *lenPtr = readBuf_->tailroom();
      printf("-- %s:%u buf=0x%p len=%zu\n", __func__, __LINE__, *bufPtr, *lenPtr);
   }
   void readDataAvailable(size_t readLen) noexcept override {
      printf("++ just read %zu bytes\n", readLen);
      readBuf_->append(readLen);

      if (todoBuf_) {
         printf("++ appending.\n");
         todoBuf_->appendChain(std::move(readBuf_));
      } else {
         printf("++ assigning.\n");
         todoBuf_ = std::move(readBuf_);
         assert(todoBuf_);
      }

      baton_.post();
   }
   void readEOF() noexcept override {
      if (destroyed_) {
         /*
          * The connection is being destroyed_ and we're getting this cb as
          * a result of shutting down the server socket.
          */
         return;
      }
      auto connShared = GetSharedPtr();
      server_->RemoveConn(connShared);
   }
   void readErr(const AsyncSocketException& except) noexcept override {
      printf("-- %s:%u\n", __func__, __LINE__);
      follib_stop_test();
   }

private:
   void InitOnEventBase(int fd);
   void DoWorkFunc();

   std::shared_ptr<AsyncSocket> sock_;
   std::shared_ptr<TestServer>  server_;
   std::unique_ptr<IOBuf>       readBuf_;
   std::unique_ptr<IOBuf>       todoBuf_;
   folly::fibers::Baton         baton_;
   bool                         destroyed_{false};
   bool                         exit_{false};
};


TestConn::~TestConn()
{
   assert(exit_);

   auto mgr = follib_get_mgr();
   destroyed_ = true;
   printf("%u: deleting connection w/ fd=%d\n", mgr->idx, sock_->getFd());
   sock_.reset();
}


std::shared_ptr<TestConn>
TestConn::newConn(std::shared_ptr<TestServer> server,
                  int fd)
{
   /*
    * This is where/how we decide what manager is going to host that
    * new connection.
    */
   auto thId = server->GetConnThreadId();
   auto evb  = follib_get_evb(thId);
   auto conn = std::make_shared<TestConn>(server);

   std::weak_ptr<TestConn> connWeak = conn;

   auto func = [connWeak, fd]() {
      if (auto c = connWeak.lock()) {
         c->InitOnEventBase(fd);
      }
   };

   evb->runInEventBaseThread(func);
   return conn;
}


void
TestConn::DoWorkFunc()
{
   while (!exit_) {
      baton_.wait();
      baton_.reset();
      printf("-- %s:%u -- Doing some work.\n", __func__, __LINE__);
      while (true) {
         if (!todoBuf_) {
            printf("-- %s:%u\n", __func__, __LINE__);
            break;
         }

         IOBuf *buf = &*todoBuf_;
         do {
            auto s = buf->moveToFbString();
            auto isEOLFunc = [](char x){ return x == '\n' || x == '\r'; };
            s.erase(std::remove_if(s.begin(), s.end(), isEOLFunc), s.end());

            printf("-- '%s'\n", s.c_str());

            buf = buf->next();
         } while (buf != &*todoBuf_);
      }
   }
}


void
TestConn::InitOnEventBase(int fd)
{
   auto mgr = follib_get_mgr();

   printf("%u: initing  connection w/ fd=%d\n", mgr->idx, fd);
   sock_ = AsyncSocket::newSocket(follib_get_evb(), fd);
   sock_->setReadCB(this);

   std::weak_ptr<TestConn> connWeak = GetSharedPtr();

   mgr->manager->addTask([connWeak]() {
      if (auto c = connWeak.lock()) {
         c->DoWorkFunc();
      }
   });
}


TestServer::~TestServer()
{
   assert(!acceptSock_);
   assert(connMap_.empty());
}


void
TestServer::StopConn(std::shared_ptr<TestConn> conn)
{
   auto evb = conn->GetEventBase();

   std::weak_ptr<TestConn> connWeak = conn;

   evb->runInEventBaseThread([connWeak]() {
      if (auto c = connWeak.lock()) {
         c->StopWork();
         c.reset();
      }
   });
}


void
TestServer::RemoveConn(std::shared_ptr<TestConn> conn)
{
   printf("-- %s:%u\n", __func__, __LINE__);

   mutex_.lock();
   connMap_.erase(conn->GetFd());
   mutex_.unlock();

   StopConn(conn);
}


void
TestServer::AddConn(int fd)
{
   auto thisShared = GetSharedPtr();
   auto conn = TestConn::newConn(thisShared, fd);

   mutex_.lock();
   connMap_[fd] = conn;
   mutex_.unlock();
}


int
TestServer::InitOnEventBase(const std::string& addrStr,
                            uint16_t           port)
{
   auto mgr = follib_get_mgr();
   auto evb = follib_get_evb();

   auto addr = SocketAddress(addrStr, port);
   printf("%u: init server at %s:%u\n", mgr->idx, addrStr.c_str(), port);

   acceptSock_ = AsyncServerSocket::newSocket(evb);

   try {
      acceptSock_->setReusePortEnabled(true);
      acceptSock_->bind(addr);
      acceptSock_->listen(10);

      /*
       * IIUC the reason addAcceptCallback() takes an event base as input, is
       * because that event base may not be the same one as the one where sock is
       * handled. This can be used to load-balance automatically.
       *
       * In our case, we schedule the accept callback to run on the same event
       * base as the one where acceptSock_ is located. This way *we* get to
       * decide where the connection is handled.
       */
      acceptSock_->addAcceptCallback(this, evb);
      acceptSock_->startAccepting();
   } catch (const std::system_error& ex) {
      acceptSock_.reset();
      int err = ex.code().value();
      printf("Failed to start accept socket: %s (%d)\n", ex.what(), err);
      return err;
   } catch (const std::exception& ex) {
      acceptSock_.reset();
      printf("Failed to start accept socket: %s\n", ex.what());
      return EINVAL;
   }

   return 0;
}


int
TestServer::StartAccept(const char *addrStr,
                        uint16_t    port,
                        uint32_t    threadId)
{
   folly::fibers::Baton baton;
   printf("start accept %s:%u on thread %u\n", addrStr, port, threadId);

   std::string addrString = addrStr;
   auto evb = follib_get_evb(threadId);
   int err = 0;

   evb->runInEventBaseThread([&]() {
      err = InitOnEventBase(addrStr, port);
      baton.post();
    });

   baton.wait();

   return err;
}


void
TestServer::ExitOnEventBase()
{
   auto mgr = follib_get_mgr();

   printf("%u: stopping server on event base\n", mgr->idx);

   acceptSock_->removeAcceptCallback(this, follib_get_evb());
   acceptSock_.reset();
}


void
TestServer::StopAccept()
{
   folly::fibers::Baton baton;

   printf("stopping accept server\n");

   auto evb = acceptSock_->getEventBase();
   evb->runInEventBaseThread([&]() {
      ExitOnEventBase();
      baton.post();
   });

   baton.wait();
   printf("server accept stopped\n");
}


void
TestServer::Exit()
{
   printf("deleting server\n");

   StopAccept();

   mutex_.lock();
   for (auto p : connMap_) {
      StopConn(p.second);
   }
   connMap_.clear();
   mutex_.unlock();
}


void
test_server()
{
   printf("----- %s -----\n", __func__);

   follib_init();

   {
      auto server = std::make_shared<TestServer>();

      auto res = server->StartAccept("127.0.0.1", 1666, 1 /* thread #0 */);
      if (res != 0) {
         goto done;
      }

      printf("Waiting for ctrl-c.\n");
      follib_run_loop(false);
      printf("Got ctrl-c.\n");

done:
      server->Exit();

      follib_run_loop_until_no_ready();
   }

   follib_exit();
}
