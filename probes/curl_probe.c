/* curl_probe.c - phase 0 link probe: libcurl static link with TLS works on this
 * node, and both portal hosts answer. No credentials are sent.
 *   $ mcr []curl_probe <ca-bundle-file>
 * Prints libcurl/TLS versions and the HTTP status of each URL (expect 401 for
 * /vsp/whoami without a token, 200 for the Keycloak realm document). */
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

static size_t sink(char *p, size_t s, size_t n, void *u)
{
    size_t *total = (size_t *)u;
    (void)p;
    *total += s * n;
    return s * n;
}

static int get(const char *url, const char *ca)
{
    CURL *h = curl_easy_init();
    CURLcode rc;
    long code = 0;
    size_t total = 0;
    char err[CURL_ERROR_SIZE];
    if (!h)
        return 0;
    err[0] = '\0';
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_CAINFO, ca);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &total);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "vupdate-probe/0");
    rc = curl_easy_perform(h);
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
    printf("%-70s rc=%d http=%ld bytes=%lu %s\n", url, (int)rc, code, (unsigned long)total,
           rc ? err : "");
    curl_easy_cleanup(h);
    return rc == CURLE_OK;
}

int main(int argc, char **argv)
{
    const curl_version_info_data *v;
    int ok;
    if (argc < 2) {
        printf("usage: curl_probe <ca-bundle>\n");
        return 1;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    v = curl_version_info(CURLVERSION_NOW);
    printf("libcurl %s, TLS %s\n", v->version, v->ssl_version ? v->ssl_version : "(none)");
    ok = get("https://sp.vmssoftware.com/vsp/whoami", argv[1]);
    ok &= get("https://kc.teracloud.com/realms/vmssoftware", argv[1]);
    curl_global_cleanup();
    printf(ok ? "PROBE-OK\n" : "PROBE-FAILED\n");
    return 1;
}
