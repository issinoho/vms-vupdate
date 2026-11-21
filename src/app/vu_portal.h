/* vu_portal.h - a signed-in session with the VSI Service Platform: token
 * management and the catalogue and detail calls (docs/NOTES.md N2-N3). */
#ifndef VU_PORTAL_H
#define VU_PORTAL_H

#include "vu_auth.h"
#include "vu_store.h"

#include <time.h>

/* Return codes. */
#define VU_OK 0
#define VU_E_NET -1    /* no reply: DNS, TLS, timeout (err says which) */
#define VU_E_AUTH -2   /* Keycloak refused the credentials (err holds its reason) */
#define VU_E_PORTAL -3 /* the portal answered with an error status or {"success":false} */
#define VU_E_REPLY -4  /* a reply of an unexpected shape: the API may have changed */
#define VU_E_MEM -5

typedef struct {
    char user[VU_USER_MAX];
    char password[VU_PASSWORD_MAX];
    vu_token tok;
    time_t tok_time; /* when tok was obtained; 0 = none */
    int requests;    /* portal requests made (for the log line) */
    char err[512];
} vu_session;

/* Prepare a session for user/password (nothing is sent yet). */
void vu_session_init(vu_session *s, const char *user, const char *password);
/* Wipe the password and token. */
void vu_session_end(vu_session *s);

/* Get a token now (password grant). VU_OK, VU_E_AUTH, VU_E_NET or VU_E_REPLY. */
int vu_portal_signin(vu_session *s);

/* GET /vsp/whoami: VU_OK when the token is accepted. *json gets the reply (free()). */
int vu_portal_whoami(vu_session *s, char **json);

/* PUT /vsp/pkgman/packages {"sysarch":[arch]}. *json gets the reply text (free()). */
int vu_portal_catalog(vu_session *s, const char *arch, char **json);

/* GET /vsp/pkgman/packages/package with X-Pkg-Id. *json gets the reply (free()). */
int vu_portal_detail(vu_session *s, long package_number, char **json);

#endif
