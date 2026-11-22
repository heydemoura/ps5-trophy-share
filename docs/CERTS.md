# Root certificates

The payload does TLS with [BearSSL](https://bearssl.org) and its own list of root certificates
(`src/trust_anchors.h`), not the console's.

Why: a console that can run payloads is stuck on old firmware, so its certificate store
(`/system/common/cert/CA_LIST.cer`) never gets newer roots. On firmware 12.20 it already cannot
validate today's Cloudflare certificates. The payload's list is updated with the payload.

The list is small on purpose: the roots that Cloudflare-hosted sites chain to (Let's Encrypt,
Google Trust Services, SSL.com, GlobalSign) plus a few common ones (DigiCert, Amazon, Sectigo)
for people who run their own server elsewhere. The PEM files are in `certs/`.

To change the list, edit `certs/` and regenerate the header with BearSSL's `brssl` tool:

```
make -C third_party/BearSSL          # builds third_party/BearSSL/build/brssl for your PC
make anchors BRSSL=third_party/BearSSL/build/brssl
```

If the console's clock is far behind (a dead clock battery, for instance), certificates look
"not valid yet". The payload never uses a time earlier than its own build time, which covers that.

Error numbers in the log: `1062` = the server's certificate is not signed by a root in the list,
`1054` = certificate expired or the clock is wrong, `1056` = certificate is for another name.
