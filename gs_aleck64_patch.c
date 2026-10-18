/*
 * gs_aleck64_patch - patch a GameShark Pro v3.30 firmware dump (builds
 * "09:54 Mar 27" and "15:56 Apr 4") so that
 * Aleck64 (CIC-NUS-5101) games that load 0x3FE000 bytes can be started
 * from the GameShark menu.
 *
 * What the patch does to shell.bin (the GameShark menu program):
 *
 *   1. The game-launch routine (0x802005DC in the Mar 27 build, 0x802005E4
 *      in the Apr 4 build) is replaced by a new one placed over unused
 *      PC-link helpers at 0x80205C00. The new routine recognises
 *      the 5101 IPL3 by two signature words, subtracts 0x100000 from the
 *      header entry point like the real 5101 IPL3 does, and, when the
 *      resulting load address is 0x80000400, copies 0x3FE000 bytes instead
 *      of 1 MB. The loader stub and the F0/F1 patch list are then placed
 *      just below the stack at the top of RAM, above the copied area. All
 *      other games are launched exactly as before. The routine is used both
 *      with and without the code generator (trainer.bin) enabled.
 *   2. The loader stub (0x8021840C / 0x80218A8C) takes its word count from
 *      $a3 instead of the hard-coded 0x40000.
 *
 * The shell is recompressed with a bit-exact reimplementation of the
 * firmware's LZARI packer and the file table is rebuilt. Because the
 * firmware bytes change, every key code changes too: the key code table
 * is regenerated (and a CIC-NUS-5101 key is added) and the header is set to
 * the key of the CIC selected with --boot.
 *
 * Usage:
 *   gs_aleck64_patch --rom6105 <file> [options] <input> <output>
 *     --rom6105 <file>  a ROM (or its first 0x1000 bytes) with a 6105 IPL3,
 *                       needed to compute the "Zelda" key code (required)
 *     --boot <cic>      key to write into the header: 6102 (default),
 *                       6103, 6105, 6106 or 5101
 *
 * The output is written in the byte order of the input (.z64/.n64/.v64).
 *
 * Copyright (c) 2026 Nekokabu
 * SPDX-License-Identifier: MIT
 */

#define _CRT_SECURE_NO_WARNINGS

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzari.h"

#define VERSION "1.1.0"

/* ---- firmware layout (v3.30) ---------------------------------------- */
#define FW_SIZE          0x40000u
#define FW_VISIBLE_END   0x20000u   /* the IPL3 sees zeros beyond this    */
#define FW_FILE_TABLE    0x3B20u    /* first file entry                   */
#define FW_FILE_END      0x2EF00u   /* 0x2EF00-0x2F100 hold live data     */
#define FW_KEY_TABLE     0x2FC00u
#define FW_KEY_TABLE_LEN 0x400u
#define KEY_ENTRY_SIZE   0x2Cu
#define KEY_NAME_LEN     31
#define HDR_ENTRY        0x08
#define HDR_CRC1         0x10
#define HDR_CRC2         0x14
#define HDR_BUILD_INFO   0x30
#define FILE_HDR_SIZE    0x10u

#define SHELL_BASE       0x80200400u
#define SHELL_MAX_SIZE   0x41000u

#define CHECK_START      0x1000u
#define CHECK_LENGTH     0x100000u
#define IPL3_6105_TABLE  0x750u

/* ---- the patch -------------------------------------------------------- */

#define LAUNCH_WORDS     79   /* size of the new launch routine (launch.s) */
#define FREE_ADDR        0x80205C00u
#define FREE_WORDS       82   /* unused PC-link helpers 0x80205C00-0x80205D47 */

/*
 * Everything that differs between the supported v3.30 builds. The new launch
 * routine is the same source (launch.s) assembled with each build's
 * addresses; see the comments in launch.s.
 */
typedef struct {
    const char *build;              /* header 0x30, 12 characters */
    uint32_t    shell_size;         /* decompressed shell.bin */
    uint32_t    shell_crc32;
    uint32_t    launch_addr;        /* original game-launch routine */
    uint32_t    launch_old[2];      /* its first two words */
    uint32_t    stub_count_addr;    /* "lui $k0, 4" in the loader stub */
    uint32_t    launch_code[LAUNCH_WORDS];
} FwVersion;

static const FwVersion versions[] = {
    {
        "09:54 Mar 27", 0x3FC20u, 0x65B194D9u,
        0x802005DCu, { 0x3C028022u, 0x8C427BD0u },
        0x80218410u,
        {
        0x3C08B000u, 0x8D100008u, 0x3C028022u, 0x8C437BD0u, 0x10600002u, 0x00000000u,
        0x8C507BD4u, 0x3C140004u, 0x8D090524u, 0x3C0A3C0Au, 0x354A003Fu, 0x152A0014u,
        0x00000000u, 0x8D090528u, 0x3C0A354Au, 0x354ADFFFu, 0x152A000Fu, 0x00000000u,
        0x3C0A0010u, 0x14600002u, 0x00000000u, 0x020A8023u, 0x3C0A8000u, 0x354A0400u,
        0x160A0007u, 0x00000000u, 0x3C14000Fu, 0x3694F800u, 0x27B2FD00u, 0x26530100u,
        0x1000000Cu, 0x00000000u, 0x3C028010u, 0x0050102Bu, 0x3C0A0010u, 0x10400005u,
        0x00000000u, 0x3C128000u, 0x36520400u, 0x10000002u, 0x00000000u, 0x020A9021u,
        0x26531000u, 0x00001825u, 0x02402825u, 0x3C048022u, 0x2484840Cu, 0x8C820000u,
        0x24840004u, 0x24630001u, 0xACA20000u, 0x24A50004u, 0x28620040u, 0x1440FFF9u,
        0x00000000u, 0x00001825u, 0x02602825u, 0x3C048025u, 0x2484B110u, 0x8C820000u,
        0x24840004u, 0x24630001u, 0xACA20000u, 0x24A50004u, 0x28620064u, 0x1440FFF9u,
        0x00000000u, 0x0C08614Fu, 0x00000000u, 0x3C11B000u, 0x36311000u, 0x02002025u,
        0x02202825u, 0x02603025u, 0x02803825u, 0x0240F809u, 0x00000000u, 0x1000FFFFu,
        0x00000000u,
        },
    },
    {
        "15:56 Apr 4 ", 0x402C0u, 0x6E14E38Bu,
        0x802005E4u, { 0x3C028023u, 0x8C428260u },
        0x80218A90u,
        {
        0x3C08B000u, 0x8D100008u, 0x3C028023u, 0x8C438260u, 0x10600002u, 0x00000000u,
        0x8C508264u, 0x3C140004u, 0x8D090524u, 0x3C0A3C0Au, 0x354A003Fu, 0x152A0014u,
        0x00000000u, 0x8D090528u, 0x3C0A354Au, 0x354ADFFFu, 0x152A000Fu, 0x00000000u,
        0x3C0A0010u, 0x14600002u, 0x00000000u, 0x020A8023u, 0x3C0A8000u, 0x354A0400u,
        0x160A0007u, 0x00000000u, 0x3C14000Fu, 0x3694F800u, 0x27B2FD00u, 0x26530100u,
        0x1000000Cu, 0x00000000u, 0x3C028010u, 0x0050102Bu, 0x3C0A0010u, 0x10400005u,
        0x00000000u, 0x3C128000u, 0x36520400u, 0x10000002u, 0x00000000u, 0x020A9021u,
        0x26531000u, 0x00001825u, 0x02402825u, 0x3C048022u, 0x24848A8Cu, 0x8C820000u,
        0x24840004u, 0x24630001u, 0xACA20000u, 0x24A50004u, 0x28620040u, 0x1440FFF9u,
        0x00000000u, 0x00001825u, 0x02602825u, 0x3C048025u, 0x2484B7C0u, 0x8C820000u,
        0x24840004u, 0x24630001u, 0xACA20000u, 0x24A50004u, 0x28620064u, 0x1440FFF9u,
        0x00000000u, 0x0C0862EFu, 0x00000000u, 0x3C11B000u, 0x36311000u, 0x02002025u,
        0x02202825u, 0x02603025u, 0x02803825u, 0x0240F809u, 0x00000000u, 0x1000FFFFu,
        0x00000000u,
        },
    },
};
#define VERSION_COUNT ((int)(sizeof versions / sizeof versions[0]))

typedef char launch_code_fits[(LAUNCH_WORDS <= FREE_WORDS) ? 1 : -1];

/* launch routine -> "j 0x80205C00 ; nop" */
static const uint32_t jump_new[2] = { 0x08000000u | ((FREE_ADDR & 0x0FFFFFFFu) >> 2), 0x00000000u };
/* loader stub: "lui $k0, 4" -> "or $k0, $a3, $zero" (word count from $a3) */
static const uint32_t count_old[1] = { 0x3C1A0004u };
static const uint32_t count_new[1] = { 0x00E0D025u };
/* first words of the unused helpers at 0x80205C00 (same in both builds) */
static const uint32_t free_old[4] = { 0x27BDFFD8u, 0xAFB20018u, 0x24120063u, 0xAFB00010u };

typedef struct {
    uint32_t addr;                 /* RAM address inside shell.bin */
    size_t   count;                /* words replaced */
    const uint32_t *expect;        /* original words (verified before patching) */
    size_t   expect_count;
    const uint32_t *replace;       /* new words */
} Site;

/* ---- key codes -------------------------------------------------------- */

enum { CIC_6102, CIC_6103, CIC_6105, CIC_6106, CIC_5101, CIC_COUNT };

typedef struct {
    const char *name;
    uint32_t    seed;
    uint32_t    entry;
    const char *gs_name;
} CicInfo;

static const CicInfo CIC[CIC_COUNT] = {
    { "6102", 0xF8CA4DDCu, 0x80180000u, "Mario World 64 & Others"     },
    { "6103", 0xA3886759u, 0x80201000u, "Diddy, 1080, Banjo, Griffey" },
    { "6105", 0xDF26F436u, 0x80190000u, "Zelda"                       },
    { "6106", 0x1FEA617Au, 0x80200400u, "Yoshis, F-Zero, C'World"     },
    { "5101", 0x95104FDDu, 0x80201000u, "Aleck64 (CIC-5101)"          },
};

/* ---- helpers ---------------------------------------------------------- */

#if defined(__GNUC__)
#define PRINTF_LIKE(f, a) __attribute__((format(printf, f, a)))
#else
#define PRINTF_LIKE(f, a)
#endif

static void die(const char *fmt, ...) PRINTF_LIKE(1, 2);
static void die(const char *fmt, ...)
{
    va_list ap;

    fflush(stdout);
    fputs("error: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static uint32_t rol32(uint32_t v, uint32_t bits)
{
    bits &= 31;
    return bits ? (v << bits) | (v >> (32 - bits)) : v;
}

static uint32_t crc32_calc(const uint8_t *p, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int k;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint8_t *read_file(const char *path, size_t limit, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    size_t n;

    if (!f)
        die("cannot open %s", path);
    buf = malloc(limit);
    if (!buf)
        die("out of memory");
    n = fread(buf, 1, limit, f);
    fclose(f);
    *size = n;
    return buf;
}

typedef enum { ORDER_Z64, ORDER_V64, ORDER_N64 } ByteOrder;

static ByteOrder detect_order(const uint8_t *d)
{
    if (d[0] == 0x80 && d[1] == 0x37 && d[2] == 0x12 && d[3] == 0x40) return ORDER_Z64;
    if (d[0] == 0x37 && d[1] == 0x80 && d[2] == 0x40 && d[3] == 0x12) return ORDER_V64;
    if (d[0] == 0x40 && d[1] == 0x12 && d[2] == 0x37 && d[3] == 0x80) return ORDER_N64;
    die("input does not start with an N64 ROM header");
    return ORDER_Z64;
}

/* convert between big endian and the given order (the swap is its own inverse) */
static void swap_order(uint8_t *d, size_t size, ByteOrder order)
{
    size_t i;
    uint8_t t;

    if (order == ORDER_V64) {
        for (i = 0; i + 1 < size; i += 2) { t = d[i]; d[i] = d[i + 1]; d[i + 1] = t; }
    } else if (order == ORDER_N64) {
        for (i = 0; i + 3 < size; i += 4) {
            t = d[i];     d[i]     = d[i + 3]; d[i + 3] = t;
            t = d[i + 1]; d[i + 1] = d[i + 2]; d[i + 2] = t;
        }
    }
}

static const char *order_name(ByteOrder o)
{
    return o == ORDER_Z64 ? "z64, big endian" : o == ORDER_V64 ? "v64, byte swapped" : "n64, little endian";
}

/* ---- IPL3 checksum ---------------------------------------------------- */

static void ipl3_checksum(const uint8_t *region, uint32_t length, int cic,
                          const uint8_t *table, uint32_t *crc1, uint32_t *crc2)
{
    uint32_t seed = CIC[cic].seed;
    uint32_t t1 = seed, t2 = seed, t3 = seed, t4 = seed, t5 = seed, t6 = seed;
    uint32_t i, d, r;

    for (i = 0; i < length; i += 4) {
        d = be32(region + i);
        if (t6 + d < t6)
            t4++;
        t6 += d;
        t3 ^= d;
        r = rol32(d, d);
        t5 += r;
        if (t2 < d)
            t2 ^= t6 ^ d;
        else
            t2 ^= r;
        if (cic == CIC_6105)
            t1 += be32(table + (i & 0xFF)) ^ d;
        else
            t1 += t5 ^ d;
    }
    if (cic == CIC_6103 || cic == CIC_5101) {
        *crc1 = (t6 ^ t4) + t3;
        *crc2 = (t5 ^ t2) + t1;
    } else if (cic == CIC_6106) {
        *crc1 = t6 * t4 + t3;
        *crc2 = t5 * t2 + t1;
    } else {
        *crc1 = t6 ^ t4 ^ t3;
        *crc2 = t5 ^ t2 ^ t1;
    }
}

static uint8_t check_digit(const uint8_t *fw, uint32_t crc1, uint32_t crc2, uint32_t entry)
{
    uint32_t sum = crc1 + crc2 + entry;
    int i;

    for (i = 0; i < 4; i++)
        sum += be32(fw + HDR_BUILD_INFO + i * 4);
    return (uint8_t)sum;
}

/* ---- file table ------------------------------------------------------- */

typedef struct {
    size_t   offset;     /* of the entry (size field) */
    uint32_t size;       /* entry size including the 0x10 header */
    char     name[13];
} FileEntry;

static int read_file_table(const uint8_t *fw, FileEntry *ent, int max)
{
    size_t p = FW_FILE_TABLE;
    int n = 0;

    while (n < max && p + FILE_HDR_SIZE <= FW_FILE_END) {
        uint32_t sz = be32(fw + p);
        if (sz == 0 || sz == 0xFFFFFFFFu || fw[p + 4] == 0 || fw[p + 4] == 0xFF)
            break;
        if (sz < FILE_HDR_SIZE + 4 || p + sz > FW_FILE_END)
            die("corrupt file table entry at 0x%lX", (unsigned long)p);
        ent[n].offset = p;
        ent[n].size = sz;
        memcpy(ent[n].name, fw + p + 4, 12);
        ent[n].name[12] = 0;
        n++;
        p += sz;
    }
    return n;
}

/* ---- main ------------------------------------------------------------- */

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s --rom6105 <file> [options] <input firmware> <output firmware>\n"
        "\n"
        "Patch a GameShark Pro v3.30 firmware so that Aleck64 (CIC-NUS-5101)\n"
        "games loading 0x3FE000 bytes can be started from the GameShark menu.\n"
        "\n"
        "Options:\n"
        "  --rom6105 <file>  ROM (or its first 0x1000 bytes) with a 6105 IPL3;\n"
        "                    needed to compute the \"Zelda\" key code (required)\n"
        "  --boot <cic>      key code written to the header: 6102 (default),\n"
        "                    6103, 6105, 6106 or 5101\n"
        "  -h, --help        show this help\n"
        "  --version         show the version\n",
        prog);
}

int main(int argc, char **argv)
{
    const char *in_path = NULL, *out_path = NULL, *rom6105_path = NULL;
    int boot = CIC_6102;
    uint8_t *fw, *rom6105 = NULL, *shell, *packed, *check;
    size_t fw_size, rom_size, shell_len, packed_len, used, p, i;
    ByteOrder order;
    const FwVersion *ver;
    FileEntry ent[16];
    int nent, k, c;
    uint8_t keytab[FW_KEY_TABLE_LEN];
    int keyn = 0;

    for (k = 1; k < argc; k++) {
        const char *a = argv[k];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(stdout, argv[0]); return 0; }
        if (!strcmp(a, "--version")) { printf("gs_aleck64_patch %s\n", VERSION); return 0; }
        if (!strcmp(a, "--rom6105")) {
            if (++k >= argc) die("--rom6105 needs a file name");
            rom6105_path = argv[k];
        } else if (!strcmp(a, "--boot")) {
            if (++k >= argc) die("--boot needs a CIC name");
            for (c = 0; c < CIC_COUNT; c++)
                if (!strcmp(argv[k], CIC[c].name)) break;
            if (c == CIC_COUNT) die("unknown CIC: %s", argv[k]);
            boot = c;
        } else if (a[0] == '-' && a[1]) {
            die("unknown option: %s", a);
        } else if (!in_path) {
            in_path = a;
        } else if (!out_path) {
            out_path = a;
        } else {
            die("too many arguments");
        }
    }
    if (!in_path || !out_path) { usage(stderr, argv[0]); return 1; }
    if (!rom6105_path)
        die("--rom6105 is required: the key code table must keep all of its entries\n"
            "       (the menu stores the selected key by its position in the table)");

    /* ---- load and verify the firmware ---- */
    fw = read_file(in_path, FW_SIZE + 1, &fw_size);
    if (fw_size != FW_SIZE)
        die("%s is not a 256 KB GameShark firmware dump (got %lu bytes)", in_path, (unsigned long)fw_size);
    order = detect_order(fw);
    swap_order(fw, fw_size, order);
    printf("Firmware : %s  [%s]\n", in_path, order_name(order));

    for (k = 0; k < VERSION_COUNT; k++)
        if (memcmp(fw + HDR_BUILD_INFO, versions[k].build, 12) == 0)
            break;
    if (k == VERSION_COUNT)
        die("unsupported firmware (build string \"%.12s\"); supported: v3.30 \"09:54 Mar 27\", \"15:56 Apr 4\"",
            fw + HDR_BUILD_INFO);
    ver = &versions[k];
    printf("Build    : v3.30 \"%.12s\"\n", ver->build);
    nent = read_file_table(fw, ent, 16);
    if (nent < 1 || strcmp(ent[0].name, "shell.bin") != 0)
        die("file table does not start with shell.bin");
    printf("Files    :");
    for (k = 0; k < nent; k++)
        printf(" %s(0x%lX)", ent[k].name, (unsigned long)ent[k].size);
    printf("\n");

    shell = malloc(SHELL_MAX_SIZE);
    if (!shell) die("out of memory");
    shell_len = lzari_decode(fw + ent[0].offset + FILE_HDR_SIZE, ent[0].size - FILE_HDR_SIZE,
                             shell, SHELL_MAX_SIZE, &used);
    if (shell_len != ver->shell_size)
        die("shell.bin did not decompress to 0x%lX bytes", (unsigned long)ver->shell_size);
    printf("shell.bin: 0x%lX bytes, CRC32 %08" PRIX32, (unsigned long)shell_len, crc32_calc(shell, shell_len));
    if (crc32_calc(shell, shell_len) != ver->shell_crc32) {
        printf("\n");
        die("shell.bin CRC32 is not %08" PRIX32 " (already patched, or modified)", ver->shell_crc32);
    }
    printf("  (OK)\n");

    /* ---- patch the shell ---- */
    {
        Site sites[3];

        sites[0].addr = ver->launch_addr;     sites[0].count = 2;
        sites[0].expect = ver->launch_old;    sites[0].expect_count = 2;  sites[0].replace = jump_new;
        sites[1].addr = ver->stub_count_addr; sites[1].count = 1;
        sites[1].expect = count_old;          sites[1].expect_count = 1;  sites[1].replace = count_new;
        sites[2].addr = FREE_ADDR;            sites[2].count = LAUNCH_WORDS;
        sites[2].expect = free_old;           sites[2].expect_count = 4;  sites[2].replace = ver->launch_code;

        for (k = 0; k < 3; k++) {
            size_t off = sites[k].addr - SHELL_BASE;

            if (off + sites[k].count * 4 > shell_len)
                die("patch site 0x%08" PRIX32 " is outside shell.bin", sites[k].addr);
            for (i = 0; i < sites[k].expect_count; i++)
                if (be32(shell + off + i * 4) != sites[k].expect[i])
                    die("unexpected code at 0x%08" PRIX32 " (word %lu is %08" PRIX32 ", expected %08" PRIX32 ")",
                        sites[k].addr, (unsigned long)i, be32(shell + off + i * 4), sites[k].expect[i]);
            for (i = 0; i < sites[k].count; i++)
                put_be32(shell + off + i * 4, sites[k].replace[i]);
            printf("Patched  : 0x%08" PRIX32 " (%lu words)\n", sites[k].addr, (unsigned long)sites[k].count);
        }
    }

    /* ---- recompress and verify ---- */
    packed = malloc(SHELL_MAX_SIZE);
    check = malloc(SHELL_MAX_SIZE);
    if (!packed || !check) die("out of memory");
    packed_len = lzari_encode(shell, shell_len, packed, SHELL_MAX_SIZE);
    if (packed_len == (size_t)-1)
        die("compression failed");
    if (lzari_decode(packed, packed_len, check, SHELL_MAX_SIZE, &used) != shell_len ||
        memcmp(check, shell, shell_len) != 0)
        die("internal error: recompressed shell.bin does not decompress to the patched data");
    printf("Repacked : shell.bin 0x%lX -> 0x%lX bytes\n",
           (unsigned long)(ent[0].size - FILE_HDR_SIZE), (unsigned long)packed_len);

    /* ---- rebuild the file area ---- */
    {
        uint8_t *area = malloc(FW_FILE_END - FW_FILE_TABLE);
        size_t total = FILE_HDR_SIZE + packed_len;

        if (!area) die("out of memory");
        for (k = 1; k < nent; k++)
            total += ent[k].size;
        if (FW_FILE_TABLE + total > FW_FILE_END)
            die("rebuilt file area is too large (0x%lX bytes, max 0x%lX)",
                (unsigned long)total, (unsigned long)(FW_FILE_END - FW_FILE_TABLE));
        memset(area, 0xFF, FW_FILE_END - FW_FILE_TABLE);
        p = 0;
        put_be32(area + p, (uint32_t)(FILE_HDR_SIZE + packed_len));
        memcpy(area + p + 4, fw + ent[0].offset + 4, 12);
        memcpy(area + p + FILE_HDR_SIZE, packed, packed_len);
        p += FILE_HDR_SIZE + packed_len;
        for (k = 1; k < nent; k++) {
            memcpy(area + p, fw + ent[k].offset, ent[k].size);
            p += ent[k].size;
        }
        memcpy(fw + FW_FILE_TABLE, area, FW_FILE_END - FW_FILE_TABLE);
        free(area);
        printf("File area: ends at 0x%lX (limit 0x%lX)\n",
               (unsigned long)(FW_FILE_TABLE + p), (unsigned long)FW_FILE_END);
    }

    /* ---- key codes ---- */
    {
        rom6105 = read_file(rom6105_path, 0x1000, &rom_size);
        if (rom_size < 0x1000)
            die("%s is shorter than 0x1000 bytes", rom6105_path);
        swap_order(rom6105, rom_size, detect_order(rom6105));
        if (crc32_calc(rom6105 + 0x40, 0x1000 - 0x40) != 0x98BC2C86u)
            die("%s does not contain a CIC-NUS-6105 IPL3", rom6105_path);
    }
    {
        /* what the IPL3 sees: firmware up to 0x20000, zeros beyond */
        uint8_t *region = calloc(CHECK_LENGTH, 1);
        uint8_t key[13];

        if (!region) die("out of memory");
        memcpy(region, fw + CHECK_START, FW_VISIBLE_END - CHECK_START);
        memset(keytab, 0, sizeof keytab);
        printf("\nNew key codes:\n");
        for (c = 0; c < CIC_COUNT; c++) {
            uint32_t crc1, crc2;
            uint8_t *e;

            ipl3_checksum(region, CHECK_LENGTH, c, rom6105 + IPL3_6105_TABLE, &crc1, &crc2);
            put_be32(key, crc1);
            put_be32(key + 4, crc2);
            put_be32(key + 8, CIC[c].entry);
            key[12] = check_digit(fw, crc1, crc2, CIC[c].entry);
            e = keytab + (size_t)keyn * KEY_ENTRY_SIZE;
            memcpy(e, key, 13);
            strncpy((char *)e + 13, CIC[c].gs_name, KEY_NAME_LEN);
            keyn++;
            printf("  %-28s  %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X  (CIC %s)\n",
                   CIC[c].gs_name, key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7],
                   key[8], key[9], key[10], key[11], key[12], CIC[c].name);
            if (c == boot) {
                put_be32(fw + HDR_ENTRY, CIC[c].entry);
                put_be32(fw + HDR_CRC1, crc1);
                put_be32(fw + HDR_CRC2, crc2);
            }
        }
        free(region);
        memcpy(fw + FW_KEY_TABLE, keytab, sizeof keytab);
        printf("Header   : entry %08" PRIX32 ", CRC %08" PRIX32 " %08" PRIX32 " (CIC %s key)\n",
               be32(fw + HDR_ENTRY), be32(fw + HDR_CRC1), be32(fw + HDR_CRC2), CIC[boot].name);
    }

    /* ---- write ---- */
    swap_order(fw, fw_size, order);
    {
        FILE *f = fopen(out_path, "wb");
        if (!f) die("cannot create %s", out_path);
        if (fwrite(fw, 1, fw_size, f) != fw_size) die("write error on %s", out_path);
        fclose(f);
    }
    printf("\nWritten  : %s  [%s]\n", out_path, order_name(order));
    return 0;
}
