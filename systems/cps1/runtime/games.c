/* The game table: where each chip of a game goes and which CPS-B its board
 * has (cps1.h). Chip placement and CPS-B values: docs/sources.md (CPS-1). */
#include "cps1.h"

#define K 1024
/* S224B (MAME's mapper_S224B): one bank of $8000 units; sprites, scroll1,
 * scroll3 and scroll2 in that order. */
#define MAPPER_S224B {{0x8000}, 4, {{CPS1_GFX_SPRITES, 0x0000, 0x43FF, 0}, {CPS1_GFX_SCROLL1, 0x4400, 0x4BFF, 0}, \
                                    {CPS1_GFX_SCROLL3, 0x4C00, 0x5FFF, 0}, {CPS1_GFX_SCROLL2, 0x6000, 0x7FFF, 0}}}
/* CPS-B-04: ID $0004 at $20, no multiplier; layer control $2E, priority
 * masks $26, $30, $28, $32, palette control $2A (Final Fight writes its
 * six-page $3F there). */
#define CPSB_04 {0x20, 0x0004, CPS1_NONE, CPS1_NONE, CPS1_NONE, CPS1_NONE, 0x2E, {0x26, 0x30, 0x28, 0x32}, 0x2A, \
                 {0x02, 0x04, 0x08, 0x30, 0x30}}

const Cps1Game cps1_games[] = {
    {"ffight", "Final Fight (World)", CPS1_PLAIN, {1024 * K, 2048 * K, 64 * K, 256 * K}, CPSB_04, 12, {
        {0xf9a5ce83, 128 * K, CPS1_R_MAIN, CPS1_LOAD_EVEN, 0x00000},    /* ff_36.11f */
        {0x65f11215, 128 * K, CPS1_R_MAIN, CPS1_LOAD_ODD, 0x00001},     /* ff_42.11h */
        {0xe1033784, 128 * K, CPS1_R_MAIN, CPS1_LOAD_EVEN, 0x40000},    /* ff_37.12f */
        {0x995e968a, 128 * K, CPS1_R_MAIN, CPS1_LOAD_ODD, 0x40001},     /* ffe_43.12h */
        {0xc747696e, 512 * K, CPS1_R_MAIN, CPS1_LOAD_SWAP16, 0x80000},  /* ff-32m.8h */
        {0x9c284108, 512 * K, CPS1_R_GFX, CPS1_LOAD_GFX4, 0x000000},    /* ff-5m.7a */
        {0xa7584dfb, 512 * K, CPS1_R_GFX, CPS1_LOAD_GFX4, 0x000002},    /* ff-7m.9a */
        {0x0b605e44, 512 * K, CPS1_R_GFX, CPS1_LOAD_GFX4, 0x000004},    /* ff-1m.3a */
        {0x52291cd2, 512 * K, CPS1_R_GFX, CPS1_LOAD_GFX4, 0x000006},    /* ff-3m.5a */
        {0xb8367eb5, 64 * K, CPS1_R_SOUND, CPS1_LOAD_PLAIN, 0x00000},   /* ff_09.12b */
        {0x375c66e7, 128 * K, CPS1_R_OKI, CPS1_LOAD_PLAIN, 0x00000},    /* ff_18.11c */
        {0x1ef137f9, 128 * K, CPS1_R_OKI, CPS1_LOAD_PLAIN, 0x20000},    /* ff_19.12c */
    }, MAPPER_S224B},
};
const int cps1_game_count = (int)(sizeof cps1_games / sizeof cps1_games[0]);
