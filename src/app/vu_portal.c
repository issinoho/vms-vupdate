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
static int call(vu_session *s, const char *method, const char *path, const char *extra,
                const char *body, char **json)
{
    char url[512], auth[VU_TOKEN_MAX + 32];
    const char *hdr[5];
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
