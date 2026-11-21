/* vu_store.c - see vu_store.h. OpenSSL libcrypto (SSL3$LIBCRYPTO_SHR32 on OpenVMS). */
#include "vu_store.h"
#include "vu_auth.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>

#define SALT 16
#define IV 12
#define TAG 16
#define PT_MAX (VU_USER_MAX + VU_PASSWORD_MAX)

static int derive(const char *nodekey, const unsigned char *salt, unsigned char key[32])
{
    static const char label[] = "VUPDATE store v1";
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    unsigned int len = 32;
    int ok = c && EVP_DigestInit_ex(c, EVP_sha256(), NULL)
             && EVP_DigestUpdate(c, label, sizeof label) /* includes the NUL */
             && EVP_DigestUpdate(c, nodekey, strlen(nodekey) + 1)
             && EVP_DigestUpdate(c, salt, SALT) && EVP_DigestFinal_ex(c, key, &len);
    EVP_MD_CTX_free(c);
    return ok;
}

/* AES-256-GCM, magic as additional data. enc=1 seals (tag out), 0 opens (tag in). */
static int gcm(int enc, const unsigned char *key, const unsigned char *iv, unsigned char *tag,
               const unsigned char *in, int n, unsigned char *out)
{
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int len, ok;
    ok = c && EVP_CipherInit_ex(c, EVP_aes_256_gcm(), NULL, NULL, NULL, enc)
         && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, IV, NULL)
         && EVP_CipherInit_ex(c, NULL, NULL, key, iv, enc)
         && EVP_CipherUpdate(c, NULL, &len, (const unsigned char *)VU_STORE_MAGIC,
                             (int)strlen(VU_STORE_MAGIC))
         && EVP_CipherUpdate(c, out, &len, in, n);
    if (ok && !enc)
        ok = EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, TAG, tag);
    if (ok)
        ok = EVP_CipherFinal_ex(c, out + len, &len);
    if (ok && enc)
        ok = EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, TAG, tag);
    EVP_CIPHER_CTX_free(c);
    return ok;
}

int vu_store_seal(const char *nodekey, const char *user, const char *password, char *out,
                  size_t outsz)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char key[32], pt[PT_MAX], blob[SALT + IV + TAG + PT_MAX];
    size_t ul = strlen(user), pl = strlen(password), n, i, o;
    int rc = -5;
    if (ul >= VU_USER_MAX || pl >= VU_PASSWORD_MAX)
        return -1;
    n = ul + 1 + pl + 1;
    o = strlen(VU_STORE_MAGIC);
    if (o + 1 + 2 * (SALT + IV + TAG + n) + 2 > outsz)
        return -1;
    memcpy(pt, user, ul + 1);
    memcpy(pt + ul + 1, password, pl + 1);
    if (RAND_bytes(blob, SALT + IV) == 1 && derive(nodekey, blob, key)
        && gcm(1, key, blob + SALT, blob + SALT + IV, pt, (int)n, blob + SALT + IV + TAG)) {
        memcpy(out, VU_STORE_MAGIC, o);
        out[o++] = '\n';
        for (i = 0; i < SALT + IV + TAG + n; i++) {
            out[o++] = hex[blob[i] >> 4];
            out[o++] = hex[blob[i] & 15];
        }
        out[o++] = '\n';
        out[o] = '\0';
        rc = 0;
    }
    vu_wipe(key, sizeof key);
    vu_wipe(pt, sizeof pt);
    vu_wipe(blob, sizeof blob);
    return rc;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int vu_store_open(const char *nodekey, const char *text, char *user, size_t usersz,
                  char *password, size_t passwordsz)
{
    unsigned char key[32], pt[PT_MAX], blob[SALT + IV + TAG + PT_MAX];
    size_t ml = strlen(VU_STORE_MAGIC), n = 0, ul, pl;
    const char *p;
    int rc = -5;
    user[0] = password[0] = '\0';
    if (strncmp(text, VU_STORE_MAGIC, ml) != 0 || (text[ml] != '\n' && text[ml] != '\r'))
        return -1;
    p = text + ml;
    while (*p == '\n' || *p == '\r')
        p++;
    while (hexval(p[0]) >= 0 && hexval(p[1]) >= 0) {
        if (n == sizeof blob)
            return -1;
        blob[n++] = (unsigned char)(hexval(p[0]) << 4 | hexval(p[1]));
        p += 2;
    }
    while (*p == '\n' || *p == '\r')
        p++;
    if (*p || n < SALT + IV + TAG + 2)
        return -1;
    n -= SALT + IV + TAG;
    if (!derive(nodekey, blob, key))
        goto done;
    if (!gcm(0, key, blob + SALT, blob + SALT + IV, blob + SALT + IV + TAG, (int)n, pt)) {
        rc = -2;
        goto done;
    }
    ul = strlen((const char *)pt);
    if (ul + 1 >= n || pt[n - 1] != '\0') {
        rc = -1;
        goto done;
    }
    pl = strlen((const char *)pt + ul + 1);
    if (ul >= usersz || pl >= passwordsz) {
        rc = -1;
        goto done;
    }
    memcpy(user, pt, ul + 1);
    memcpy(password, pt + ul + 1, pl + 1);
    rc = 0;
done:
    vu_wipe(key, sizeof key);
    vu_wipe(pt, sizeof pt);
    vu_wipe(blob, sizeof blob);
    return rc;
}
