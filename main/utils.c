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
