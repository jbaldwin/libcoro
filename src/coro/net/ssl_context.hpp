#pragma once

#include <filesystem>
#include <mutex>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

namespace coro::net
{
class tcp_client;

enum class ssl_file_type : int
{
    /// The file is of type ASN1
    asn1 = SSL_FILETYPE_ASN1,
    /// The file is of type PEM
    pem = SSL_FILETYPE_PEM
};

/**
 * SSL context, used with client or server types to provide secure connections.
 */
class ssl_context
{
public:
    /**
     * Creates a context with no certificate and no private key, maybe useful for testing.
     */
    ssl_context();

    /**
     * Creates a context with the given certificate and the given private key.
     * @param certificate The location of the certificate file.
     * @param certificate_type See `ssl_file_type`.
     * @param private_key The location of the private key file.
     * @param private_key_type See `ssl_file_type`.
     */
    ssl_context(
        std::filesystem::path certificate,
        ssl_file_type         certificate_type,
        std::filesystem::path private_key,
        ssl_file_type         private_key_type);
    ~ssl_context();

private:
    static uint64_t   m_ssl_context_count;
    static std::mutex m_ssl_context_mutex;

    SSL_CTX* m_ssl_ctx{nullptr};

    /// The following classes use the underlying SSL_CTX* object for performing SSL functions.
    friend tcp_client;

    auto native_handle() -> SSL_CTX* { return m_ssl_ctx; }
    auto native_handle() const -> const SSL_CTX* { return m_ssl_ctx; }
};

} // namespace coro::net
