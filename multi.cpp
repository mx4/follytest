#include "follib_log.h"

#include "test_file_io.h"
#include "test_net_server.h"
#include "test_server.h"


int
main(int argc, char* argv[])
{
   follib_log_init(argv[0]);

//   test_file_io();

   test_net_server();

//   test_server();

   follib_log_exit();

   return 0;
}
