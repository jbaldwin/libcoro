#ifdef LIBCORO_FEATURE_TLS

    #pragma once

    #include <filesystem>
    #include <mutex>

    #include <openssl/bio.h>
    #include <openssl/err.h>
    #include <openssl/pem.h>
    #include <openssl/ssl.h>

namespace coro::net::tls
{
class client;

enum class tls_file_type : int
{
    /// The file is of type ASN1
    asn1 = SSL_FILETYPE_ASN1,
    /// The file is of type PEM
    pem = SSL_FILETYPE_PEM
};

enum class verify_peer_t : int
{
    yes,
    no
};

/**
 * TLS context, used with client or server types to provide secure connections.
 */
class context
{
public:
    /**
     * Creates a context with no certificate and no private key, maybe useful for testing.
     * @param verify_peer Should the peer be verified? Defaults to true.
     */
    explicit context(verify_peer_t verify_peer = verify_peer_t::yes);

    /**
     * Creates a context with the given certificate and the given private key.
     * @param certificate The location of the certificate file.
     * @param certificate_type See `tls_file_type`.
     * @param private_key The location of the private key file.
     * @param private_key_type See `tls_file_type`.
     * @param verify_perr Should the peer be verified? Defaults to true.
     */
    context(
        std::filesystem::path certificate,
        tls_file_type         certificate_type,
        std::filesystem::path private_key,
        tls_file_type         private_key_type,
        verify_peer_t         verify_peer = verify_peer_t::yes);
    ~context();

private:
    SSL_CTX* m_ssl_ctx{nullptr};

    /// The following classes use the underlying SSL_CTX* object for performing SSL functions.
    friend client;

    auto native_handle() -> SSL_CTX* { return m_ssl_ctx; }
    auto native_handle() const -> const SSL_CTX* { return m_ssl_ctx; }
};

} // namespace coro::net::tls

#endif // #ifdef LIBCORO_FEATURE_TLS
