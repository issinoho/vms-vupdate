/* vu_http.h - the few HTTPS requests VUPDATE makes, over one libcurl easy handle. */
#ifndef VU_HTTP_H
#define VU_HTTP_H

#include <stddef.h>
#include <stdio.h>

typedef struct {
    char *data;   /* the body, NUL-terminated (malloc); NULL on transport failure */
    size_t len;
    long status;  /* HTTP status, 0 on transport failure */
    char err[256];/* transport error text */
} vu_resp;

/* cainfo: the CA bundle file; proxy: NULL or a proxy URL (logical VUPDATE$PROXY).
 * Returns 0, or -1 when libcurl can't be initialised. */
int vu_http_init(const char *cainfo, const char *proxy);
void vu_http_cleanup(void);

/* method "GET", "POST" or "PUT"; headers a NULL-terminated list of "Name: value"
 * lines; body NULL or the request body. Replies larger than maxbytes fail.
 * Returns 0 when a reply arrived (any status), -1 on transport failure. */
int vu_http_request(const char *method, const char *url, const char *const *headers,
                    const char *body, size_t maxbytes, vu_resp *r);
void vu_resp_free(vu_resp *r);

/* GET url into out (binary). A 2xx body goes to out; any other status's body is
 * kept (first 1 KB) in r->data instead. r->len is the number of bytes written to
 * out. Returns 0 when a reply arrived (check r->status), -1 on transport failure. */
int vu_http_download(const char *url, const char *const *headers, FILE *out, vu_resp *r);

#endif
