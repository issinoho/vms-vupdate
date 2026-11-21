/* vu_http.c - see vu_http.h. libcurl from vms-curl (D3). */
#include "vu_http.h"
#include "vu_auth.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

#include "vu_version.h"
#define VU_USER_AGENT "VUPDATE/" VU_VERSION " (OpenVMS)"

static CURL *h;
static char ca[512], px[512];

typedef struct {
    vu_resp *r;
    size_t cap, max;
    int over;
} sink_t;

static size_t sink(char *p, size_t s, size_t n, void *u)
{
    sink_t *k = (sink_t *)u;
    size_t add = s * n;
    if (k->r->len + add > k->max) {
        k->over = 1;
        return 0;
    }
    if (k->r->len + add + 1 > k->cap) {
        size_t nc = k->cap ? k->cap : 16384;
        char *nd;
        while (nc < k->r->len + add + 1)
            nc *= 2;
        nd = (char *)realloc(k->r->data, nc);
        if (!nd)
            return 0;
        k->r->data = nd;
        k->cap = nc;
    }
    memcpy(k->r->data + k->r->len, p, add);
    k->r->len += add;
    k->r->data[k->r->len] = '\0';
    return add;
}

int vu_http_init(const char *cainfo, const char *proxy)
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK || !(h = curl_easy_init()))
        return -1;
    strncpy(ca, cainfo ? cainfo : "", sizeof ca - 1);
    strncpy(px, proxy ? proxy : "", sizeof px - 1);
    return 0;
}

void vu_http_cleanup(void)
{
    if (h)
        curl_easy_cleanup(h);
    h = NULL;
    curl_global_cleanup();
}

void vu_resp_free(vu_resp *r)
{
    if (r->data) {
        vu_wipe(r->data, r->len); /* token replies hold the bearer token */
        free(r->data);
    }
    r->data = NULL;
    r->len = 0;
}

int vu_http_request(const char *method, const char *url, const char *const *headers,
                    const char *body, size_t maxbytes, vu_resp *r)
{
    struct curl_slist *hl = NULL;
    sink_t k;
    CURLcode rc;
    char eb[CURL_ERROR_SIZE];
    r->data = NULL;
    r->len = 0;
    r->status = 0;
    r->err[0] = eb[0] = '\0';
    if (!h) {
        strcpy(r->err, "HTTP not initialised");
        return -1;
    }
    for (; headers && *headers; headers++)
        hl = curl_slist_append(hl, *headers);
    k.r = r;
    k.cap = 0;
    k.max = maxbytes;
    k.over = 0;
    curl_easy_reset(h);
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_USERAGENT, VU_USER_AGENT);
    if (ca[0])
        curl_easy_setopt(h, CURLOPT_CAINFO, ca);
    if (px[0])
        curl_easy_setopt(h, CURLOPT_PROXY, px);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &k);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, eb);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, ""); /* gzip/zstd when the server offers */
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, body ? body : "");
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, body ? body : "");
    }
    rc = curl_easy_perform(h);
    curl_slist_free_all(hl);
    /* POSTFIELDS points at the caller's body (it may hold the password): forget it. */
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, NULL);
    if (rc != CURLE_OK) {
        strncpy(r->err, k.over ? "reply too large" : eb[0] ? eb : curl_easy_strerror(rc),
                sizeof r->err - 1);
        r->err[sizeof r->err - 1] = '\0';
        vu_resp_free(r);
        return -1;
    }
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &r->status);
    if (!r->data) {
        r->data = (char *)malloc(1);
        if (!r->data) {
            strcpy(r->err, "out of memory");
            return -1;
        }
        r->data[0] = '\0';
    }
    return 0;
}

typedef struct {
    FILE *out;
    vu_resp *r;
    int checked, ok, failed;
} dl_t;

static size_t dl_sink(char *p, size_t s, size_t n, void *u)
{
    dl_t *d = (dl_t *)u;
    size_t add = s * n;
    if (!d->checked) {
        long code = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
        d->ok = code >= 200 && code <= 299;
        d->checked = 1;
    }
    if (d->ok) {
        if (fwrite(p, 1, add, d->out) != add) {
            d->failed = 1;
            return 0;
        }
        d->r->len += add;
        return add;
    }
    /* Error body: keep the start for the message. */
    if (!d->r->data && (d->r->data = (char *)calloc(1025, 1)) == NULL)
        return 0;
    {
        size_t have = strlen(d->r->data), room = 1024 - have;
        memcpy(d->r->data + have, p, add < room ? add : room);
    }
    return add;
}

int vu_http_download(const char *url, const char *const *headers, FILE *out, vu_resp *r)
{
    struct curl_slist *hl = NULL;
    dl_t d;
    CURLcode rc;
    char eb[CURL_ERROR_SIZE];
    r->data = NULL;
    r->len = 0;
    r->status = 0;
    r->err[0] = eb[0] = '\0';
    if (!h) {
        strcpy(r->err, "HTTP not initialised");
        return -1;
    }
    for (; headers && *headers; headers++)
        hl = curl_slist_append(hl, *headers);
    memset(&d, 0, sizeof d);
    d.out = out;
    d.r = r;
    curl_easy_reset(h);
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_USERAGENT, VU_USER_AGENT);
    if (ca[0])
        curl_easy_setopt(h, CURLOPT_CAINFO, ca);
    if (px[0])
        curl_easy_setopt(h, CURLOPT_PROXY, px);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, dl_sink);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, eb);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
    /* Large kits on slow links: give up only when under 1 KB/s for 2 minutes. */
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 120L);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    rc = curl_easy_perform(h);
    curl_slist_free_all(hl);
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &r->status);
    if (rc != CURLE_OK) {
        strncpy(r->err, d.failed ? "cannot write the kit file" : eb[0] ? eb : curl_easy_strerror(rc),
                sizeof r->err - 1);
        r->err[sizeof r->err - 1] = '\0';
        free(r->data);
        r->data = NULL;
        return -1;
    }
    return 0;
}
