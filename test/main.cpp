#define CATCH_CONFIG_MAIN
#include "catch_amalgamated.hpp"

#include <signal.h>

#ifdef LIBCORO_FEATURE_NETWORKING
/**
 * This structure invokes a constructor to setup some global test settings that are needed prior
 * to executing the tests.
 */
struct test_setup_networking
{
    test_setup_networking()
    {
        // Ignore SIGPIPE, the library should be handling these gracefully.
        signal(SIGPIPE, SIG_IGN);

    #ifdef LIBCORO_FEATURE_TLS
        // For SSL/TLS tests create a localhost cert.pem and key.pem, tests expected these files
        // to be generated into the same directory that the tests are running in.
        auto unused = system(
            "openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -subj '/CN=localhost' -nodes");
        (void)unused;
    #endif
    }

    ~test_setup_networking()
    {
        // Cleanup the temporary key.pem and cert.pem files.
        auto unused = system("rm key.pem cert.pem");
        (void)unused;
    }
};

static test_setup_networking g_test_setup_networking{};
#endif
