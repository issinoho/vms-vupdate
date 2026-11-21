/* test_store.c - the credential store codec (src/app/vu_store.c, OpenSSL).
 * Linux: make test; the nodes: @build_vms TEST. No real credentials: the values
 * below are made up. */
#include "vu_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;

#define CHECK(c)                                                                         \
    do {                                                                                 \
        checks++;                                                                        \
        if (!(c)) {                                                                      \
            fails++;                                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
        }                                                                                \
    } while (0)

int main(void)
{
    char t1[VU_STORE_TEXT_MAX], t2[VU_STORE_TEXT_MAX], u[VU_USER_MAX], p[VU_PASSWORD_MAX];
    char big[VU_PASSWORD_MAX + 8];
    const char *key = "NODE1|1025|VSI x86-64 test";
    size_t ml = strlen(VU_STORE_MAGIC);
    char *h;

    CHECK(vu_store_seal(key, "someone@example.com", "p&ss w0rd!", t1, sizeof t1) == 0);
    CHECK(strncmp(t1, VU_STORE_MAGIC "\n", ml + 1) == 0);
    CHECK(strstr(t1, "someone") == NULL && strstr(t1, "w0rd") == NULL);
    CHECK(vu_store_open(key, t1, u, sizeof u, p, sizeof p) == 0);
    CHECK(strcmp(u, "someone@example.com") == 0 && strcmp(p, "p&ss w0rd!") == 0);

    /* Random salt and IV: sealing twice gives different text, both open. */
    CHECK(vu_store_seal(key, "someone@example.com", "p&ss w0rd!", t2, sizeof t2) == 0);
    CHECK(strcmp(t1, t2) != 0);
    CHECK(vu_store_open(key, t2, u, sizeof u, p, sizeof p) == 0);

    /* Another node's key: refused, outputs empty. */
    CHECK(vu_store_open("NODE2|1026|VSI x86-64 test", t1, u, sizeof u, p, sizeof p) == -2);
    CHECK(u[0] == '\0' && p[0] == '\0');

    /* One changed hex digit in the ciphertext: refused (GCM tag). */
    h = t1 + strlen(t1) - 3;
    *h = *h == '0' ? '1' : '0';
    CHECK(vu_store_open(key, t1, u, sizeof u, p, sizeof p) == -2);

    /* Not a store. */
    CHECK(vu_store_open(key, "hello\n", u, sizeof u, p, sizeof p) == -1);
    CHECK(vu_store_open(key, VU_STORE_MAGIC "\nzz\n", u, sizeof u, p, sizeof p) == -1);
    CHECK(vu_store_open(key, VU_STORE_MAGIC "\n00\n", u, sizeof u, p, sizeof p) == -1);

    /* CRLF line ends (a file copied through a PC) still open. */
    {
        char crlf[VU_STORE_TEXT_MAX + 4];
        size_t n;
        strcpy(crlf, VU_STORE_MAGIC "\r\n");
        strcat(crlf, t2 + ml + 1);
        n = strlen(crlf);
        crlf[n - 1] = '\r';
        crlf[n] = '\n';
        crlf[n + 1] = '\0';
        CHECK(vu_store_open(key, crlf, u, sizeof u, p, sizeof p) == 0);
    }

    /* Too long, and too small an output buffer. */
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    CHECK(vu_store_seal(key, "a@b", big, t2, sizeof t2) == -1);
    CHECK(vu_store_seal(key, "a@b", "pw", t2, 40) == -1);
    /* Empty password is allowed (Keycloak decides). */
    CHECK(vu_store_seal(key, "a@b", "", t2, sizeof t2) == 0);
    CHECK(vu_store_open(key, t2, u, sizeof u, p, sizeof p) == 0 && p[0] == '\0');

    printf("store: %d checks, %d failed\n", checks, fails);
    return fails ? EXIT_FAILURE : EXIT_SUCCESS;
}
