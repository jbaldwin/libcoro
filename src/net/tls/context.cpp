#include "coro/net/tls/context.hpp"

#include <iostream>

namespace coro::net::tls
{
static uint64_t   g_tls_context_count{0};
static std::mutex g_tls_context_mutex{};

context::context(verify_peer_t verify_peer)
{
    {
        std::scoped_lock g{g_tls_context_mutex};
        if (g_tls_context_count == 0)
        {
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L
            OPENSSL_init_ssl(0, nullptr);
#else
            SSL_library_init();
#endif
        }
        ++g_tls_context_count;
    }

#if !defined(LIBRESSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L
    m_ssl_ctx = SSL_CTX_new(TLS_method());
#else
    m_ssl_ctx = SSL_CTX_new(SSLv23_method());
#endif
    if (m_ssl_ctx == nullptr)
    {
        throw std::runtime_error{"Failed to initialize OpenSSL Context object."};
    }

    // Disable SSLv3
    SSL_CTX_set_options(m_ssl_ctx, SSL_OP_ALL | SSL_OP_NO_SSLv3);
    // Abort handshake if certificate verification fails.
    if (verify_peer == verify_peer_t::yes)
    {
        SSL_CTX_set_verify(m_ssl_ctx, SSL_VERIFY_PEER, NULL);
    }
    // Set the minimum TLS version, as of this TLSv1.1 or earlier are deprecated.
    SSL_CTX_set_min_proto_version(m_ssl_ctx, TLS1_2_VERSION);
}

context::context(
    std::filesystem::path certificate,
    tls_file_type         certificate_type,
    std::filesystem::path private_key,
    tls_file_type         private_key_type,
    verify_peer_t         verify_peer)
    : context(verify_peer)
{
    if (auto r = SSL_CTX_use_certificate_file(m_ssl_ctx, certificate.c_str(), static_cast<int>(certificate_type));
        r != 1)
    {
        throw std::runtime_error{"Failed to load certificate file " + certificate.string()};
    }

    if (auto r = SSL_CTX_use_PrivateKey_file(m_ssl_ctx, private_key.c_str(), static_cast<int>(private_key_type));
        r != 1)
    {
        throw std::runtime_error{"Failed to load private key file " + private_key.string()};
    }

    if (auto r = SSL_CTX_check_private_key(m_ssl_ctx); r != 1)
    {
        throw std::runtime_error{"Certificate and private key do not match."};
    }
}

context::~context()
{
    if (m_ssl_ctx != nullptr)
    {
        SSL_CTX_free(m_ssl_ctx);
        m_ssl_ctx = nullptr;
    }
}

} // namespace coro::net::tls
