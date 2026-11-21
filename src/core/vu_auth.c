/* vu_auth.c - see vu_auth.h. */
#include "vu_auth.h"
#include "cJSON.h"

#include <string.h>

void vu_wipe(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--)
        *v++ = 0;
}

int vu_form_escape(const char *in, char *out, size_t outsz)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        int plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                    || c == '-' || c == '.' || c == '_' || c == '*';
        if (o + (plain ? 1 : 3) >= outsz)
            return -1;
        if (plain) {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    if (o >= outsz)
        return -1;
    out[o] = '\0';
    return (int)o;
}

static int append(char *out, size_t outsz, size_t *o, const char *s)
{
    size_t l = strlen(s);
    if (*o + l >= outsz)
        return -1;
    memcpy(out + *o, s, l + 1);
    *o += l;
    return 0;
}

int vu_token_body(const char *user, const char *password, char *out, size_t outsz)
{
    size_t o = 0;
    int n;
    if (outsz == 0 || append(out, outsz, &o, "client_id=sp&grant_type=password&username="))
        return -1;
    if ((n = vu_form_escape(user, out + o, outsz - o)) < 0)
        return -1;
    o += (size_t)n;
    if (append(out, outsz, &o, "&password="))
        return -1;
    if ((n = vu_form_escape(password, out + o, outsz - o)) < 0)
        return -1;
    o += (size_t)n;
    return (int)o;
}

static void scopy(char *dst, size_t sz, const char *src)
{
    size_t l = strlen(src);
    if (l >= sz)
        l = sz - 1;
    memcpy(dst, src, l);
    dst[l] = '\0';
}

int vu_token_parse(const char *json, vu_token *t)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *v;
    int rc = -1;
    t->access[0] = '\0';
    t->expires_in = 0;
    t->error[0] = '\0';
    if (!cJSON_IsObject(root)) {
        scopy(t->error, sizeof t->error, "token reply is not a JSON object");
        goto done;
    }
    v = cJSON_GetObjectItemCaseSensitive(root, "access_token");
    if (cJSON_IsString(v) && v->valuestring && v->valuestring[0]) {
        if (strlen(v->valuestring) >= sizeof t->access) {
            scopy(t->error, sizeof t->error, "token too long");
            goto done;
        }
        strcpy(t->access, v->valuestring);
        v = cJSON_GetObjectItemCaseSensitive(root, "expires_in");
        if (cJSON_IsNumber(v))
            t->expires_in = (long)v->valuedouble;
        rc = 0;
        goto done;
    }
    v = cJSON_GetObjectItemCaseSensitive(root, "error_description");
    if (!(cJSON_IsString(v) && v->valuestring))
        v = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (cJSON_IsString(v) && v->valuestring) {
        scopy(t->error, sizeof t->error, v->valuestring);
        rc = -3;
    } else {
        scopy(t->error, sizeof t->error, "token reply has neither access_token nor error");
    }
done:
    if (root) {
        /* The parsed copy holds the token too. */
        v = cJSON_GetObjectItemCaseSensitive(root, "access_token");
        if (cJSON_IsString(v) && v->valuestring)
            vu_wipe(v->valuestring, strlen(v->valuestring));
        cJSON_Delete(root);
    }
    return rc;
}
