#pragma once

#include <filesystem>
#include <mutex>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

namespace coro::net
{
enum class ssl_file_type : int
{
    asn1 = SSL_FILETYPE_ASN1,
    pem  = SSL_FILETYPE_PEM
};

class ssl_context
{
public:
    ssl_context();
    ssl_context(
        std::filesystem::path certificate,
        ssl_file_type         certificate_type,
        std::filesystem::path private_key,
        ssl_file_type         private_key_type);
    ~ssl_context();

    auto native_handle() -> SSL_CTX* { return m_ssl_ctx; }
    auto native_handle() const -> const SSL_CTX* { return m_ssl_ctx; }

private:
    static uint64_t   m_ssl_context_count;
    static std::mutex m_ssl_context_mutex;

    SSL_CTX* m_ssl_ctx{nullptr};
};

} // namespace coro::net
