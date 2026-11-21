/* vu_auth.h - Keycloak password grant: the request body and the reply. No I/O.
 * The grant (docs/NOTES.md N2): POST <realm>/protocol/openid-connect/token with
 * client_id=sp&grant_type=password&username=..&password=.. (public client). */
#ifndef VU_AUTH_H
#define VU_AUTH_H

#include <stddef.h>

#define VU_TOKEN_URL "https://kc.teracloud.com/realms/vmssoftware/protocol/openid-connect/token"
#define VU_PORTAL_URL "https://sp.vmssoftware.com"
#define VU_TOKEN_MAX 8192

typedef struct {
    char access[VU_TOKEN_MAX]; /* the bearer token; never printed */
    long expires_in;           /* seconds, 0 when not given */
    char error[256];           /* Keycloak error_description (or error) on failure */
} vu_token;

/* application/x-www-form-urlencoded escape of in into out (NUL-terminated).
 * Returns the length, or -1 when out is too small. */
int vu_form_escape(const char *in, char *out, size_t outsz);

/* The grant body for user/password. Returns the length, or -1 when out is too small. */
int vu_token_body(const char *user, const char *password, char *out, size_t outsz);

/* Parse a token endpoint reply. Returns 0 with t->access set; -3 when Keycloak
 * returned an error object (t->error says why, e.g. "Invalid user credentials");
 * -1 when the reply isn't the expected JSON (t->error says what was wrong). */
int vu_token_parse(const char *json, vu_token *t);

/* Wipe a buffer the compiler won't optimise away (passwords, tokens). */
void vu_wipe(void *p, size_t n);

#endif
