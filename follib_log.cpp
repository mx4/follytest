#include <glog/logging.h>

#include "follib_log.h"

class FollibLogger : public google::base::Logger {
   void Write(bool force,
              time_t timestamp,
              const char *msg,
              int msgLen) {
      printf("%.*s", msgLen, msg);
   }
   void Flush() { }
   uint32_t LogSize() { return 0; }
};

static FollibLogger *logger;

void
follib_log_init(const char *argv0)
{
   DCHECK(!logger);

   google::InitGoogleLogging(argv0);
   google::InstallFailureSignalHandler();

   logger = new FollibLogger();

   google::base::SetLogger(google::INFO, logger);
   google::base::SetLogger(google::WARNING, logger);
   google::base::SetLogger(google::ERROR, logger);
   google::base::SetLogger(google::FATAL, logger);
   LOG(INFO) << __func__;
}

void
follib_log_exit()
{
   LOG(INFO) << __func__;
   google::ShutdownGoogleLogging();
   delete logger;
   logger = nullptr;
}
