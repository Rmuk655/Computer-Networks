#ifndef SSL_h
#define SSL_h

#include <iostream>
#include <cstring>
#include <openssl/ssl.h>
#include <openssl/err.h>

using namespace std;

struct SSLArgs
{
    string cert = "server.crt";
    string key = "server.key";
    bool enabled = false;
    bool verified = false;
};

inline void SSLInit()
{
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

inline SSL_CTX *SSLcreateserver(const SSLArgs &args)
{
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx)
    {
        cerr << "Could not create a TLS server context" << endl;
        return nullptr;
    }
    // If OpenSSL failed to load the certificate, stop.
    if (SSL_CTX_use_certificate_file(ctx, args.cert.c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        cerr << "SSL_CTX_use_certificate_file failed" << endl;
        SSL_CTX_free(ctx);
        return nullptr;
    }
    // If OpenSSL couldn't load the server's private key, stop.
    if (SSL_CTX_use_PrivateKey_file(ctx, args.key.c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        cerr << "SSL_CTX_use_PrivateKey_file failed" << endl;
        SSL_CTX_free(ctx);
        return nullptr;
    }
    // The private key loaded into ctx actually matches the public key in the certificate.
    if (!SSL_CTX_check_private_key(ctx))
    {
        cerr << "Private Key does not match certificate" << endl;
        SSL_CTX_free(ctx);
        return nullptr;
    }
    return ctx;
}

inline SSL *sslServerAccept(SSL_CTX *ctx, int clientFD)
{
    SSL *ssl = SSL_new(ctx);
    if (!ssl)
    {
        cerr << "SSL_new" << endl;
        return nullptr;
    }
    SSL_set_fd(ssl, clientFD);
    if (SSL_accept(ssl) <= 0)
    {
        cerr << "SSL_accept (handshake failed)" << endl;
        SSL_free(ssl);
        return nullptr;
    }
    cout << "SSL_accept Handshake completed on server side." << endl;
    return ssl;
}

inline SSL_CTX *SSLcreateclient(const SSLArgs &args)
{
    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx)
    {
        cerr << "Could not create a TLS client context" << endl;
        return nullptr;
    }
    // Here is the certificate/certificate authority that I trust for verifying the peer.
    if (SSL_CTX_load_verify_locations(ctx, args.cert.c_str(), nullptr) <= 0)
    {
        // If OpenSSL couldn't load the trusted certificate, abort TLS setup.
        cerr << "SSL_CTX not verified" << endl;
        SSL_CTX_free(ctx);
        return nullptr;
    }
    // During the TLS handshake, actually verify the peer's certificate.
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    return ctx;
}

inline SSL *sslClientConnect(SSL_CTX *ctx, int sockfd)
{
    SSL *ssl = SSL_new(ctx);
    if (!ssl)
    {
        cerr << "SSL_new" << endl;
        return nullptr;
    }
    SSL_set_fd(ssl, sockfd);
    if (SSL_connect(ssl) <= 0)
    {
        cerr << "SSL_accept (handshake failed)" << endl;
        SSL_free(ssl);
        return nullptr;
    }
    cout << "SSL_accept Handshake completed on client side." << endl;
    return ssl;
}


#endif