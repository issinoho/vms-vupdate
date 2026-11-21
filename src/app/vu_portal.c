/* vu_portal.c - see vu_portal.h. */
#include "vu_portal.h"
#include "vu_http.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKEN_REPLY 65536
#define MAX_REPLY (16L * 1024 * 1024)

void vu_session_init(vu_session *s, const char *user, const char *password)
{
    memset(s, 0, sizeof *s);
    strncpy(s->user, user, sizeof s->user - 1);
    strncpy(s->password, password, sizeof s->password - 1);
}

void vu_session_end(vu_session *s)
{
    vu_wipe(s, sizeof *s);
}

int vu_portal_signin(vu_session *s)
{
    static const char *const hdr[] = {"Content-Type: application/x-www-form-urlencoded",
                                      "Accept: application/json", NULL};
    char body[1024];
    vu_resp r;
    int rc;
    if (vu_token_body(s->user, s->password, body, sizeof body) < 0) {
        strcpy(s->err, "user name or password too long");
        return VU_E_AUTH;
    }
    rc = vu_http_request("POST", VU_TOKEN_URL, hdr, body, MAX_TOKEN_REPLY, &r);
    vu_wipe(body, sizeof body);
    s->tok_time = 0;
    if (rc != 0) {
        snprintf(s->err, sizeof s->err, "sign-in server: %s", r.err);
        return VU_E_NET;
    }
    rc = vu_token_parse(r.data, &s->tok);
    if (rc == 0) {
        s->tok_time = time(NULL);
        if (s->tok.expires_in <= 0)
            s->tok.expires_in = 180;
        rc = VU_OK;
    } else if (rc == -3 || r.status == 401 || r.status == 400) {
        snprintf(s->err, sizeof s->err, "refused (HTTP %ld): %s", r.status,
                 s->tok.error[0] ? s->tok.error : "no reason given");
        rc = VU_E_AUTH;
    } else {
        snprintf(s->err, sizeof s->err, "sign-in server: unexpected reply (HTTP %ld): %s",
                 r.status, s->tok.error);
        rc = VU_E_REPLY;
    }
    vu_resp_free(&r);
    return rc;
}

static int token_fresh(const vu_session *s)
{
    /* Renew 30 s before expiry: a CHECK that fetches many details outlives a token. */
    return s->tok_time && time(NULL) - s->tok_time < s->tok.expires_in - 30;
}

/* The reason from a {"success":false,"reason":...} body, or "". */
static void reason(const char *json, char *out, size_t sz)
{
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    const cJSON *v = root ? cJSON_GetObjectItemCaseSensitive(root, "reason") : NULL;
    out[0] = '\0';
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(out, v->valuestring, sz - 1);
        out[sz - 1] = '\0';
    }
    cJSON_Delete(root);
}

/* An authorised portal request; retried once with a new token on 401. */
static int call2(vu_session *s, const char *method, const char *path, const char *extra,
                 const char *extra2, const char *body, char **json)
{
    char url[512], auth[VU_TOKEN_MAX + 32];
    const char *hdr[6];
    vu_resp r;
    int tries, rc, n = 0;
    *json = NULL;
    snprintf(url, sizeof url, "%s%s", VU_PORTAL_URL, path);
    for (tries = 0; tries < 2; tries++) {
        if (!token_fresh(s) && (rc = vu_portal_signin(s)) != VU_OK)
            return rc;
        snprintf(auth, sizeof auth, "Authorization: Bearer %s", s->tok.access);
        n = 0;
        hdr[n++] = auth;
        hdr[n++] = "Accept: application/json";
        if (body)
            hdr[n++] = "Content-Type: application/json";
        if (extra)
            hdr[n++] = extra;
        if (extra2)
            hdr[n++] = extra2;
        hdr[n] = NULL;
        rc = vu_http_request(method, url, hdr, body, MAX_REPLY, &r);
        vu_wipe(auth, sizeof auth);
        s->requests++;
        if (rc != 0) {
            snprintf(s->err, sizeof s->err, "%s: %s", path, r.err);
            return VU_E_NET;
        }
        if (r.status == 401 && tries == 0) {
            vu_resp_free(&r);
            s->tok_time = 0; /* the token was refused: get a new one and try again */
            continue;
        }
        break;
    }
    if (r.status < 200 || r.status > 299) {
        char why[256];
        reason(r.data, why, sizeof why);
        snprintf(s->err, sizeof s->err, "%s: HTTP %ld%s%s", path, r.status, why[0] ? ": " : "",
                 why);
        vu_resp_free(&r);
        return VU_E_PORTAL;
    }
    *json = r.data; /* handed over: not wiped, holds no secret */
    return VU_OK;
}

static int call(vu_session *s, const char *method, const char *path, const char *extra,
                const char *body, char **json)
{
    return call2(s, method, path, extra, NULL, body, json);
}

int vu_portal_whoami(vu_session *s, char **json)
{
    return call(s, "GET", "/vsp/whoami", NULL, NULL, json);
}

int vu_portal_catalog(vu_session *s, const char *arch, char **json)
{
    char body[64];
    snprintf(body, sizeof body, "{\"sysarch\":[\"%s\"]}", arch);
    return call(s, "PUT", "/vsp/pkgman/packages", NULL, body, json);
}

int vu_portal_detail(vu_session *s, long package_number, char **json)
{
    char hdr[64];
    snprintf(hdr, sizeof hdr, "X-Pkg-Id: %ld", package_number);
    return call(s, "GET", "/vsp/pkgman/packages/package", hdr, NULL, json);
}

/* Download (N18). Step 1, GET .../package/download with X-Pkg-Id: <file number>
 * and X-Os-Version, lists download ids for the file *and the files of its
 * prerequisites*. Step 2, GET .../download/<id>, names each one (save_as, md5)
 * and gives a pre-signed object-store URL. Step 3 fetches that URL without the
 * bearer token (it is for the portal only). The URL is a short-lived credential:
 * never printed, wiped after use. */
int vu_portal_download(vu_session *s, long file_number, const char *os, const char *save_as,
                       FILE *out, unsigned long *bytes)
{
    char h1[64], h2[64], path[128], *json = NULL;
    cJSON *root, *urls;
    int rc, q, n, found = 0;
    *bytes = 0;
    snprintf(h1, sizeof h1, "X-Pkg-Id: %ld", file_number);
    snprintf(h2, sizeof h2, "X-Os-Version: %s", os);
    rc = call2(s, "GET", "/vsp/pkgman/packages/package/download", h1, h2, NULL, &json);
    if (rc != VU_OK)
        return rc;
    root = cJSON_Parse(json);
    free(json);
    urls = root ? cJSON_GetObjectItemCaseSensitive(root, "urls") : NULL;
    if (root && cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "success"))) {
        const cJSON *why = cJSON_GetObjectItemCaseSensitive(root, "reason");
        snprintf(s->err, sizeof s->err, "download: %s",
                 cJSON_IsString(why) ? why->valuestring : "refused");
        cJSON_Delete(root);
        return VU_E_PORTAL;
    }
    if (!cJSON_IsArray(urls) || (n = cJSON_GetArraySize(urls)) < 1) {
        strcpy(s->err, "download: no download ids in the reply");
        cJSON_Delete(root);
        return VU_E_REPLY;
    }
    rc = VU_OK;
    for (q = 0; q < n && !found && rc == VU_OK; q++) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(urls, q), "url");
        cJSON *r2;
        const cJSON *sa, *u;
        char *url = NULL;
        vu_resp r;
        if (!cJSON_IsString(id) || !id->valuestring
            || strspn(id->valuestring, "0123456789abcdefABCDEF-") != strlen(id->valuestring)
            || strlen(id->valuestring) > 64) {
            strcpy(s->err, "download: an id is not a UUID");
            rc = VU_E_REPLY;
            break;
        }
        snprintf(path, sizeof path, "/vsp/pkgman/packages/package/download/%s", id->valuestring);
        if ((rc = call(s, "GET", path, NULL, NULL, &json)) != VU_OK)
            break;
        r2 = cJSON_Parse(json);
        vu_wipe(json, strlen(json));
        free(json);
        sa = r2 ? cJSON_GetObjectItemCaseSensitive(r2, "save_as") : NULL;
        u = r2 ? cJSON_GetObjectItemCaseSensitive(r2, "url") : NULL;
        if (!cJSON_IsString(sa) || !sa->valuestring || !cJSON_IsString(u) || !u->valuestring
            || strncmp(u->valuestring, "https://", 8) != 0) {
            strcpy(s->err, "download: a reply has no file name or https URL");
            rc = VU_E_REPLY;
        } else if (strcmp(sa->valuestring, save_as) == 0) {
            size_t ul = strlen(u->valuestring);
            found = 1;
            if ((url = (char *)malloc(ul + 1)) != NULL)
                memcpy(url, u->valuestring, ul + 1);
            else
                rc = VU_E_MEM;
        }
        if (cJSON_IsString(u) && u->valuestring)
            vu_wipe(u->valuestring, strlen(u->valuestring));
        cJSON_Delete(r2);
        if (!url)
            continue; /* another file (a prerequisite's): not wanted */
        rc = vu_http_download(url, NULL, out, &r);
        vu_wipe(url, strlen(url));
        free(url);
        s->requests++;
        if (rc != 0) {
            snprintf(s->err, sizeof s->err, "download: %s", r.err);
            rc = VU_E_NET;
        } else if (r.status < 200 || r.status > 299) {
            snprintf(s->err, sizeof s->err, "download from the object store: HTTP %ld", r.status);
            rc = VU_E_PORTAL;
        } else {
            *bytes = (unsigned long)r.len;
            rc = VU_OK;
        }
        free(r.data);
    }
    cJSON_Delete(root);
    if (rc == VU_OK && !found) {
        snprintf(s->err, sizeof s->err, "download: %s is not among the %d files offered", save_as, n);
        rc = VU_E_REPLY;
    }
    return rc;
}
