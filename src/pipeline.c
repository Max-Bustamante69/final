#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/time.h>
#endif

/*
 * Proyecto: Reto Final - Triangulo de Hierro
 * Clase: Sistemas Operativos - C2661-SI2004-5186
 * Profesor: Edison Valencia
 * Desarrolladores: Maximiliano Bustamante, Valeria Hornung
 * Registro de desarrollo (semana de entrega: 2026-05-12 a 2026-05-18):
 * - 2026-05-12: esqueleto del pipeline y flujo CLI (Maximiliano Bustamante)
 * - 2026-05-13: modulo RLE en RAM (Valeria Hornung)
 * - 2026-05-14: XTEA-CBC y padding PKCS#7 (Maximiliano Bustamante)
 * - 2026-05-15: manejo seguro de llave en RAM (Valeria Hornung + Maximiliano Bustamante)
 * - 2026-05-18: ajustes finales de metricas y documentacion (Valeria Hornung)
 */

#define XTEA_ROUNDS 32
#define XTEA_BLOCK_SIZE 8
#define XTEA_KEY_SIZE 16

typedef struct {
    double compress_ms;
    double decompress_ms;
    double encrypt_ms;
    double decrypt_ms;
    double wall_ms;
    double cpu_ms;
    size_t input_size;
    size_t output_size;
} Metrics;

typedef struct {
    unsigned char *data;
    size_t len;
} Buffer;

static void secure_zero(void *ptr, size_t len) {
    /* 2026-05-15, Maximiliano Bustamante: borrado explicito de secretos en RAM. */
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) {
        *p++ = 0;
    }
}

static int lock_memory(void *ptr, size_t len) {
/* 2026-05-15, Valeria Hornung: mitigacion de swap para passphrase en uso. */
#ifdef _WIN32
    return VirtualLock(ptr, len) ? 0 : -1;
#else
    return mlock(ptr, len);
#endif
}

static void unlock_memory(void *ptr, size_t len) {
#ifdef _WIN32
    VirtualUnlock(ptr, len);
#else
    munlock(ptr, len);
#endif
}

static double now_wall_ms(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&counter);
    return (1000.0 * (double)counter.QuadPart) / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

static double now_cpu_ms(void) {
    return (1000.0 * (double)clock()) / (double)CLOCKS_PER_SEC;
}

static int read_file(const char *path, Buffer *out) {
    FILE *f = fopen(path, "rb");
    size_t sz;
    unsigned char *buf;
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = (size_t)ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = (unsigned char *)malloc(sz ? sz : 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (sz > 0 && fread(buf, 1, sz, f) != sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    out->data = buf;
    out->len = sz;
    return 0;
}

static int write_file(const char *path, const unsigned char *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    if (len > 0 && fwrite(buf, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* RLE basico en RAM: [count][byte] */
static int rle_compress(const unsigned char *in, size_t in_len, Buffer *out) {
    /* 2026-05-13, Valeria Hornung: implementacion de compresion base. */
    size_t i = 0;
    size_t cap = (in_len * 2) + 2;
    unsigned char *buf = (unsigned char *)malloc(cap ? cap : 1);
    size_t pos = 0;
    if (!buf) {
        return -1;
    }
    while (i < in_len) {
        unsigned char b = in[i];
        unsigned char count = 1;
        while (i + count < in_len && in[i + count] == b && count < 255) {
            count++;
        }
        buf[pos++] = count;
        buf[pos++] = b;
        i += count;
    }
    out->data = buf;
    out->len = pos;
    return 0;
}

static int rle_decompress(const unsigned char *in, size_t in_len, Buffer *out) {
    /* 2026-05-13, Valeria Hornung: descompresion simetrica para validar integridad. */
    size_t i = 0;
    size_t cap = 0;
    unsigned char *buf;
    size_t pos = 0;
    if (in_len % 2 != 0) {
        return -1;
    }
    for (i = 0; i < in_len; i += 2) {
        cap += in[i];
    }
    buf = (unsigned char *)malloc(cap ? cap : 1);
    if (!buf) {
        return -1;
    }
    for (i = 0; i < in_len; i += 2) {
        unsigned char count = in[i];
        unsigned char value = in[i + 1];
        size_t j;
        for (j = 0; j < count; j++) {
            buf[pos++] = value;
        }
    }
    out->data = buf;
    out->len = pos;
    return 0;
}

static uint32_t read_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32_le(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static void xtea_encrypt_block(uint32_t v[2], const uint32_t k[4]) {
    /* 2026-05-14, Maximiliano Bustamante: nucleo de cifrado simetrico XTEA. */
    uint32_t v0 = v[0], v1 = v[1], sum = 0, i;
    const uint32_t delta = 0x9E3779B9u;
    for (i = 0; i < XTEA_ROUNDS; i++) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]);
        sum += delta;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]);
    }
    v[0] = v0;
    v[1] = v1;
}

static void xtea_decrypt_block(uint32_t v[2], const uint32_t k[4]) {
    uint32_t v0 = v[0], v1 = v[1], i;
    const uint32_t delta = 0x9E3779B9u;
    uint32_t sum = delta * XTEA_ROUNDS;
    for (i = 0; i < XTEA_ROUNDS; i++) {
        v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]);
        sum -= delta;
        v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]);
    }
    v[0] = v0;
    v[1] = v1;
}

static void derive_xtea_key(const unsigned char *pass, size_t pass_len, unsigned char key[16]) {
    uint32_t h[4] = {2166136261u, 2166136261u ^ 0x11111111u, 2166136261u ^ 0x22222222u, 2166136261u ^ 0x33333333u};
    size_t i;
    for (i = 0; i < pass_len; i++) {
        unsigned char b = pass[i];
        h[0] = (h[0] ^ b) * 16777619u;
        h[1] = (h[1] ^ (unsigned char)(b + 0x13u)) * 16777619u;
        h[2] = (h[2] ^ (unsigned char)(b + 0x37u)) * 16777619u;
        h[3] = (h[3] ^ (unsigned char)(b + 0x71u)) * 16777619u;
    }
    write_u32_le(key + 0, h[0]);
    write_u32_le(key + 4, h[1]);
    write_u32_le(key + 8, h[2]);
    write_u32_le(key + 12, h[3]);
}

static void fill_iv(unsigned char iv[XTEA_BLOCK_SIZE]) {
    size_t i;
    uint64_t seed = (uint64_t)time(NULL);
    for (i = 0; i < XTEA_BLOCK_SIZE; i++) {
        seed ^= (seed << 13);
        seed ^= (seed >> 7);
        seed ^= (seed << 17);
        iv[i] = (unsigned char)(seed & 0xFFu);
    }
}

static int xtea_cbc_encrypt(const unsigned char *in, size_t in_len, const unsigned char key_bytes[16], Buffer *out) {
    /* 2026-05-14, Maximiliano Bustamante: pipeline CBC + padding PKCS#7. */
    size_t pad = XTEA_BLOCK_SIZE - (in_len % XTEA_BLOCK_SIZE);
    size_t padded_len = in_len + pad;
    size_t total = XTEA_BLOCK_SIZE + padded_len;
    unsigned char *buf = (unsigned char *)malloc(total);
    uint32_t key[4];
    unsigned char prev[XTEA_BLOCK_SIZE];
    size_t i;
    if (!buf) {
        return -1;
    }
    key[0] = read_u32_le(key_bytes + 0);
    key[1] = read_u32_le(key_bytes + 4);
    key[2] = read_u32_le(key_bytes + 8);
    key[3] = read_u32_le(key_bytes + 12);
    fill_iv(buf);
    memcpy(prev, buf, XTEA_BLOCK_SIZE);

    for (i = 0; i < padded_len; i += XTEA_BLOCK_SIZE) {
        unsigned char block[XTEA_BLOCK_SIZE];
        uint32_t v[2];
        size_t j;
        for (j = 0; j < XTEA_BLOCK_SIZE; j++) {
            size_t idx = i + j;
            unsigned char plain = (idx < in_len) ? in[idx] : (unsigned char)pad;
            block[j] = plain ^ prev[j];
        }
        v[0] = read_u32_le(block);
        v[1] = read_u32_le(block + 4);
        xtea_encrypt_block(v, key);
        write_u32_le(buf + XTEA_BLOCK_SIZE + i, v[0]);
        write_u32_le(buf + XTEA_BLOCK_SIZE + i + 4, v[1]);
        memcpy(prev, buf + XTEA_BLOCK_SIZE + i, XTEA_BLOCK_SIZE);
    }
    out->data = buf;
    out->len = total;
    return 0;
}

static int xtea_cbc_decrypt(const unsigned char *in, size_t in_len, const unsigned char key_bytes[16], Buffer *out) {
    /* 2026-05-14, Valeria Hornung: validacion de padding y reconstruccion de buffer plano. */
    uint32_t key[4];
    unsigned char prev[XTEA_BLOCK_SIZE];
    unsigned char *buf;
    size_t cipher_len;
    size_t i;
    unsigned char pad;
    if (in_len < XTEA_BLOCK_SIZE || (in_len - XTEA_BLOCK_SIZE) % XTEA_BLOCK_SIZE != 0) {
        return -1;
    }
    cipher_len = in_len - XTEA_BLOCK_SIZE;
    buf = (unsigned char *)malloc(cipher_len ? cipher_len : 1);
    if (!buf) {
        return -1;
    }

    key[0] = read_u32_le(key_bytes + 0);
    key[1] = read_u32_le(key_bytes + 4);
    key[2] = read_u32_le(key_bytes + 8);
    key[3] = read_u32_le(key_bytes + 12);
    memcpy(prev, in, XTEA_BLOCK_SIZE);

    for (i = 0; i < cipher_len; i += XTEA_BLOCK_SIZE) {
        unsigned char block[XTEA_BLOCK_SIZE];
        uint32_t v[2];
        size_t j;
        v[0] = read_u32_le(in + XTEA_BLOCK_SIZE + i);
        v[1] = read_u32_le(in + XTEA_BLOCK_SIZE + i + 4);
        xtea_decrypt_block(v, key);
        write_u32_le(block, v[0]);
        write_u32_le(block + 4, v[1]);
        for (j = 0; j < XTEA_BLOCK_SIZE; j++) {
            buf[i + j] = block[j] ^ prev[j];
        }
        memcpy(prev, in + XTEA_BLOCK_SIZE + i, XTEA_BLOCK_SIZE);
    }

    pad = buf[cipher_len - 1];
    if (pad == 0 || pad > XTEA_BLOCK_SIZE) {
        free(buf);
        return -1;
    }
    for (i = 0; i < pad; i++) {
        if (buf[cipher_len - 1 - i] != pad) {
            free(buf);
            return -1;
        }
    }
    out->data = buf;
    out->len = cipher_len - pad;
    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Uso: %s <modo> <input> <output>\n", prog);
    fprintf(stderr, "Modos:\n");
    fprintf(stderr, "  classic-copy      (A: copia plana)\n");
    fprintf(stderr, "  compress-only     (B: solo compresion RLE)\n");
    fprintf(stderr, "  decompress-only\n");
    fprintf(stderr, "  ce-encode         (Candidato 1: comprimir -> encriptar)\n");
    fprintf(stderr, "  ce-decode\n");
    fprintf(stderr, "  ec-encode         (Candidato 2: encriptar -> comprimir)\n");
    fprintf(stderr, "  ec-decode\n");
}

static int load_passphrase(unsigned char **pass, size_t *len) {
    /* 2026-05-15, Valeria Hornung: entrada segura por consola/env, nunca por argv. */
    const char *env = getenv("PIPELINE_KEY");
    if (env && env[0] != '\0') {
        size_t l = strlen(env);
        unsigned char *buf = (unsigned char *)malloc(l + 1);
        if (!buf) {
            return -1;
        }
        memcpy(buf, env, l + 1);
        *pass = buf;
        *len = l;
        return 0;
    }

    printf("Ingrese llave simetrica: ");
    fflush(stdout);
    {
        char local[512];
        if (!fgets(local, sizeof(local), stdin)) {
            return -1;
        }
        {
            size_t l = strcspn(local, "\r\n");
            unsigned char *buf = (unsigned char *)malloc(l + 1);
            if (!buf) {
                secure_zero(local, sizeof(local));
                return -1;
            }
            memcpy(buf, local, l);
            buf[l] = '\0';
            secure_zero(local, sizeof(local));
            *pass = buf;
            *len = l;
            return 0;
        }
    }
}

static int run_mode(const char *mode, const Buffer *in, const unsigned char *key, Metrics *m, Buffer *out) {
    /* 2026-05-12/2026-05-18, ambos: orquestacion de los 4 escenarios del parcial. */
    Buffer tmp1 = {0};
    Buffer tmp2 = {0};
    double t0, t1;
    int rc = -1;

    m->input_size = in->len;
    if (strcmp(mode, "classic-copy") == 0) {
        out->data = (unsigned char *)malloc(in->len ? in->len : 1);
        if (!out->data) {
            return -1;
        }
        memcpy(out->data, in->data, in->len);
        out->len = in->len;
        m->output_size = out->len;
        return 0;
    }

    if (strcmp(mode, "compress-only") == 0) {
        t0 = now_wall_ms();
        if (rle_compress(in->data, in->len, out) != 0) {
            return -1;
        }
        t1 = now_wall_ms();
        m->compress_ms = t1 - t0;
        m->output_size = out->len;
        return 0;
    }

    if (strcmp(mode, "decompress-only") == 0) {
        t0 = now_wall_ms();
        if (rle_decompress(in->data, in->len, out) != 0) {
            return -1;
        }
        t1 = now_wall_ms();
        m->decompress_ms = t1 - t0;
        m->output_size = out->len;
        return 0;
    }

    if (strcmp(mode, "ce-encode") == 0) {
        t0 = now_wall_ms();
        rc = rle_compress(in->data, in->len, &tmp1);
        t1 = now_wall_ms();
        if (rc != 0) {
            return -1;
        }
        m->compress_ms = t1 - t0;

        t0 = now_wall_ms();
        rc = xtea_cbc_encrypt(tmp1.data, tmp1.len, key, out);
        t1 = now_wall_ms();
        free(tmp1.data);
        if (rc != 0) {
            return -1;
        }
        m->encrypt_ms = t1 - t0;
        m->output_size = out->len;
        return 0;
    }

    if (strcmp(mode, "ce-decode") == 0) {
        t0 = now_wall_ms();
        rc = xtea_cbc_decrypt(in->data, in->len, key, &tmp1);
        t1 = now_wall_ms();
        if (rc != 0) {
            return -1;
        }
        m->decrypt_ms = t1 - t0;

        t0 = now_wall_ms();
        rc = rle_decompress(tmp1.data, tmp1.len, out);
        t1 = now_wall_ms();
        free(tmp1.data);
        if (rc != 0) {
            return -1;
        }
        m->decompress_ms = t1 - t0;
        m->output_size = out->len;
        return 0;
    }

    if (strcmp(mode, "ec-encode") == 0) {
        t0 = now_wall_ms();
        rc = xtea_cbc_encrypt(in->data, in->len, key, &tmp1);
        t1 = now_wall_ms();
        if (rc != 0) {
            return -1;
        }
        m->encrypt_ms = t1 - t0;

        t0 = now_wall_ms();
        rc = rle_compress(tmp1.data, tmp1.len, out);
        t1 = now_wall_ms();
        free(tmp1.data);
        if (rc != 0) {
            return -1;
        }
        m->compress_ms = t1 - t0;
        m->output_size = out->len;
        return 0;
    }

    if (strcmp(mode, "ec-decode") == 0) {
        t0 = now_wall_ms();
        rc = rle_decompress(in->data, in->len, &tmp1);
        t1 = now_wall_ms();
        if (rc != 0) {
            return -1;
        }
        m->decompress_ms = t1 - t0;

        t0 = now_wall_ms();
        rc = xtea_cbc_decrypt(tmp1.data, tmp1.len, key, out);
        t1 = now_wall_ms();
        free(tmp1.data);
        if (rc != 0) {
            return -1;
        }
        m->decrypt_ms = t1 - t0;
        m->output_size = out->len;
        return 0;
    }

    if (tmp1.data) {
        free(tmp1.data);
    }
    if (tmp2.data) {
        free(tmp2.data);
    }
    return -1;
}

int main(int argc, char **argv) {
    /* 2026-05-18, Maximiliano Bustamante: cierre de flujo, metricas y salida final. */
    const char *mode;
    const char *input_path;
    const char *output_path;
    Buffer input = {0};
    Buffer output = {0};
    Metrics metrics = {0};
    unsigned char *passphrase = NULL;
    size_t pass_len = 0;
    unsigned char key[XTEA_KEY_SIZE];
    double wall_start, wall_end, cpu_start, cpu_end;
    int needs_key = 0;
    int rc = 1;

    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }
    mode = argv[1];
    input_path = argv[2];
    output_path = argv[3];

    if (strcmp(mode, "ce-encode") == 0 || strcmp(mode, "ce-decode") == 0 ||
        strcmp(mode, "ec-encode") == 0 || strcmp(mode, "ec-decode") == 0) {
        needs_key = 1;
    }

    if (read_file(input_path, &input) != 0) {
        fprintf(stderr, "Error leyendo archivo de entrada: %s\n", input_path);
        return 1;
    }

    if (needs_key) {
        if (load_passphrase(&passphrase, &pass_len) != 0) {
            fprintf(stderr, "Error cargando la llave.\n");
            free(input.data);
            return 1;
        }
        if (lock_memory(passphrase, pass_len + 1) != 0) {
            fprintf(stderr, "Advertencia: no se pudo bloquear la llave en RAM (mlock/VirtualLock).\n");
        }
        derive_xtea_key(passphrase, pass_len, key);
        secure_zero(passphrase, pass_len + 1);
        unlock_memory(passphrase, pass_len + 1);
        free(passphrase);
        passphrase = NULL;
    } else {
        memset(key, 0, sizeof(key));
    }

    wall_start = now_wall_ms();
    cpu_start = now_cpu_ms();
    if (run_mode(mode, &input, key, &metrics, &output) != 0) {
        fprintf(stderr, "Error ejecutando modo %s\n", mode);
        goto cleanup;
    }
    cpu_end = now_cpu_ms();
    wall_end = now_wall_ms();
    metrics.wall_ms = wall_end - wall_start;
    metrics.cpu_ms = cpu_end - cpu_start;

    if (write_file(output_path, output.data, output.len) != 0) {
        fprintf(stderr, "Error escribiendo archivo de salida: %s\n", output_path);
        goto cleanup;
    }

    printf(
        "METRICS mode=%s input_bytes=%zu output_bytes=%zu wall_ms=%.3f cpu_ms=%.3f compress_ms=%.3f "
        "decompress_ms=%.3f encrypt_ms=%.3f decrypt_ms=%.3f\n",
        mode,
        metrics.input_size,
        metrics.output_size,
        metrics.wall_ms,
        metrics.cpu_ms,
        metrics.compress_ms,
        metrics.decompress_ms,
        metrics.encrypt_ms,
        metrics.decrypt_ms);

    rc = 0;

cleanup:
    secure_zero(key, sizeof(key));
    if (input.data) {
        free(input.data);
    }
    if (output.data) {
        free(output.data);
    }
    return rc;
}
