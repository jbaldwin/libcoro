#include "coro/net/ssl_context.hpp"

#include <iostream>

namespace coro::net
{
uint64_t   ssl_context::m_ssl_context_count{0};
std::mutex ssl_context::m_ssl_context_mutex{};

ssl_context::ssl_context()
{
    {
        std::scoped_lock g{m_ssl_context_mutex};
        if (m_ssl_context_count == 0)
        {

#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L
            OPENSSL_init_ssl(0, nullptr);
#else
            SSL_library_init();
#endif
        }
        ++m_ssl_context_count;
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
}

ssl_context::ssl_context(
    std::filesystem::path certificate,
    ssl_file_type         certificate_type,
    std::filesystem::path private_key,
    ssl_file_type         private_key_type)
    : ssl_context()
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

ssl_context::~ssl_context()
{
    if (m_ssl_ctx != nullptr)
    {
        SSL_CTX_free(m_ssl_ctx);
        m_ssl_ctx = nullptr;
    }
}

} // namespace coro::net
