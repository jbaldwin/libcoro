#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <signal.h>

/**
 * This structure invokes a constructor to setup some global test settings that are needed prior
 * to executing the tests.
 */
struct test_setup
{
    test_setup()
    {
        // Ignore SIGPIPE, the library should be handling these gracefully.
        signal(SIGPIPE, SIG_IGN);

        // For SSL/TLS tests create a localhost cert.pem and key.pem, tests expected these files
        // to be generated into the same directory that the tests are running in.
        system(
            "openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -subj '/CN=localhost' -nodes");
    }

    ~test_setup()
    {
        // Cleanup the temporary key.pem and cert.pem files.
        system("rm key.pem cert.pem");
    }
};

static test_setup g_test_setup{};
