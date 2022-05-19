#include "jdesp.h"

#include "mbedtls/md.h"
#include "mbedtls/base64.h"

char *extract_property(const char *property_bag, int plen, const char *key) {
    int klen = strlen(key);
    for (int ptr = 0; ptr + klen < plen;) {
        int nextp = ptr;
        while (nextp < plen && property_bag[nextp] != ';')
            nextp++;
        if (property_bag[ptr + klen] == '=' && memcmp(property_bag + ptr, key, klen) == 0) {
            int sidx = ptr + klen + 1;
            int rlen = nextp - sidx;
            char *r = malloc(rlen + 1);
            memcpy(r, property_bag + sidx, rlen);
            r[rlen] = 0;
            return r;
        }
        if (nextp < plen)
            nextp++;
        ptr = nextp;
    }
    return NULL;
}

char *nvs_get_str_a(nvs_handle_t handle, const char *key) {
    size_t sz = 0;
    int err = nvs_get_str(handle, key, NULL, &sz);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return NULL;
    JD_ASSERT(err == ESP_OK);
    char *res = malloc(sz);
    err = nvs_get_str(handle, key, res, &sz);
    JD_ASSERT(err == ESP_OK);
    return res;
}

void *nvs_get_blob_a(nvs_handle_t handle, const char *key, size_t *outsz) {
    int err = nvs_get_blob(handle, key, NULL, outsz);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return NULL;
    JD_ASSERT(err == ESP_OK);
    void *res = malloc(*outsz);
    err = nvs_get_blob(handle, key, res, outsz);
    JD_ASSERT(err == ESP_OK);
    return res;
}

static int urlencode_core(char *dst, const char *src) {
    int len = 0;
    while (*src) {
        uint8_t c = *src++;
        if ((48 <= c && c <= 57) || (97 <= (c | 0x20) && (c | 0x20) <= 122) ||
            (c == 45 || c == 46 || c == 95 || c == 126)) {
            if (dst)
                *dst++ = c;
            len++;
        } else {
            if (dst) {
                *dst++ = '%';
                jd_to_hex(dst, &c, 1);
                dst += 2;
            }
            len += 3;
        }
    }
    if (dst)
        *dst++ = 0;
    return len + 1;
}

char *jd_urlencode(const char *src) {
    int len = urlencode_core(NULL, src);
    char *r = jd_alloc(len);
    urlencode_core(r, src);
    return r;
}

char *jd_concat_many(const char **parts) {
    int len = 0;
    for (int i = 0; parts[i]; ++i)
        len += strlen(parts[i]);
    char *r = jd_alloc(len + 1);
    len = 0;

    for (int i = 0; parts[i]; ++i) {
        int k = strlen(parts[i]);
        memcpy(r + len, parts[i], k);
        len += k;
    }
    r[len] = 0;
    return r;
}

char *jd_concat2(const char *a, const char *b) {
    const char *arr[] = {a, b, NULL};
    return jd_concat_many(arr);
}

char *jd_concat3(const char *a, const char *b, const char *c) {
    const char *arr[] = {a, b, c, NULL};
    return jd_concat_many(arr);
}

static int jd_json_escape_core(const char *str, char *dst) {
    int len = 0;

    while (*str) {
        char c = *str++;
        int q = 0;
        switch (c) {
        case '"':
        case '\\':
            q = 1;
            break;
        case '\n':
            c = 'n';
            q = 1;
            break;
        case '\r':
            c = 'r';
            q = 1;
            break;
        case '\t':
            c = 't';
            q = 1;
            break;
        default:
            if (c >= 32) {
                len++;
                if (dst)
                    *dst++ = c;
            } else {
                len += 6;
                if (dst) {
                    *dst++ = '\\';
                    *dst++ = 'u';
                    *dst++ = '0';
                    *dst++ = '0';
                    jd_to_hex(dst, &c, 1);
                    dst += 2;
                }
            }
            break;
        }
        if (q == 1) {
            len += 2;
            if (dst) {
                *dst++ = '\\';
                *dst++ = c;
            }
        }
    }

    len++;
    if (dst)
        *dst = 0;

    return len;
}

char *jd_json_escape(const char *str) {
    int len = jd_json_escape_core(str, NULL);
    char *r = jd_alloc(len);
    jd_json_escape_core(str, r);
    return r;
}

char *jd_hmac_b64(const char *key, const char **parts) {
    uint8_t binkey[64];
    size_t klen = 0;

    if (mbedtls_base64_decode(binkey, sizeof(binkey), &klen, (const unsigned char *)key,
                              strlen(key))) {
        DMESG("invalid sign key");
        return NULL;
    }

    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    CHK(mbedtls_md_setup(&md_ctx, md_info, 1));

    if (mbedtls_md_hmac_starts(&md_ctx, binkey, klen) != 0) {
        DMESG("invalid key len?");
        mbedtls_md_free(&md_ctx);
        return NULL;
    }

    for (int i = 0; parts[i]; ++i) {
        CHK(mbedtls_md_hmac_update(&md_ctx, (const unsigned char *)parts[i], strlen(parts[i])));
    }

    CHK(mbedtls_md_hmac_finish(&md_ctx, binkey));
    mbedtls_md_free(&md_ctx);

    mbedtls_base64_encode(NULL, 0, &klen, binkey, 32);
    char *r = jd_alloc(klen + 1);
    CHK(mbedtls_base64_encode((unsigned char *)r, klen + 1, &klen, binkey, 32));
    r[klen] = 0;
    return r;
}

jd_frame_t *jd_dup_frame(const jd_frame_t *frame) {
    int sz = JD_FRAME_SIZE(frame);
    jd_frame_t *r = jd_alloc(sz);
    memcpy(r, frame, sz);
    return r;
}