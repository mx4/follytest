#include <iostream>
#include <future>
#include <thread>

#include <folly/futures/Future.h>

static uint64_t work_func(const std::string& s)
{
   uint64_t sum = 0;
   for (auto i = 0; i < s.size(); i++) {
      sum += s[i];
   }
   return sum;
}


static void test_folly_futures(const std::string& str)
{
   folly::Future<uint64_t> f = work_func(str);

   f.wait();
   std::cout << "folly::futures: " << f.value() << std::endl;
}


static void
test_packaged_task(const std::string& str)
{
   std::packaged_task<uint64_t(const std::string&)> task(work_func);
   std::future<uint64_t> f = task.get_future();
   std::thread t(std::move(task), str);

   f.wait();
   std::cout << "packaged_task:  " << f.get() << std::endl;
   t.join();
}


static void
test_async(const std::string& str)
{
   std::future<uint64_t> f = std::async(std::launch::async,
                                         work_func, str);
   f.wait();
   std::cout << "async:          " << f.get() << std::endl;
}


static void
test_promise(const std::string& str)
{
   std::promise<uint64_t> p;
   std::future<uint64_t> f = p.get_future();
   auto th = std::thread([&p, str] {
       p.set_value_at_thread_exit(work_func(str));
   });
   th.detach();
   f.wait();
   std::cout << "std::promise:   " << f.get() << std::endl;
}


int main()
{
   const std::string testStr = "this is a test";

   test_folly_futures(testStr);
   test_packaged_task(testStr);
   test_async(testStr);
   test_promise(testStr);
}
