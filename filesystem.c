/* [TYPE: IMPLEMENTATION]
 * filesystem.c | PlanktOS block filesystem over FRAM tertiary partition
 */
#pragma GCC optimize("O2")
#include "filesystem.h"
#include "fram.h"
#include <string.h>

/* ── Internal helpers ───────────────────────────────────────────── */

/* Read one index entry. Returns address and size via out-params. */
static void idx_read(uint8_t block, uint16_t *addr, uint16_t *size) {
    uint16_t base = FS_INDEX_BASE + (uint16_t)block * FS_INDEX_ENTRY_SZ;
    uint8_t  buf[4];
    fram_read_buf(base, buf, 4);
    *addr = ((uint16_t)buf[0] << 8) | buf[1];
    *size = ((uint16_t)buf[2] << 8) | buf[3];
}

/* Write one index entry. */
static void idx_write(uint8_t block, uint16_t addr, uint16_t size) {
    uint16_t base = FS_INDEX_BASE + (uint16_t)block * FS_INDEX_ENTRY_SZ;
    uint8_t  buf[4];
    buf[0] = (uint8_t)(addr >> 8);
    buf[1] = (uint8_t)(addr & 0xFF);
    buf[2] = (uint8_t)(size >> 8);
    buf[3] = (uint8_t)(size & 0xFF);
    fram_write_buf(base, buf, 4);
}

/* Mark an index slot as empty. */
static void idx_clear(uint8_t block) {
    idx_write(block, FS_SLOT_EMPTY, 0);
}

/* Returns total bytes currently used by all occupied blocks
 * (sum of their sizes). */
static uint16_t storage_used(void) {
    uint16_t used = 0;
    for (uint8_t i = 0; i < FS_MAX_BLOCKS; i++) {
        uint16_t addr, size;
        idx_read(i, &addr, &size);
        if (addr != FS_SLOT_EMPTY) used += size;
    }
    return used;
}

/* Compute total segments needed for a payload of `data_len` bytes:
 *   1 header segment + ceil(data_len / 16) data segments + 1 EOF segment */
static uint16_t segments_for(uint16_t data_len) {
    uint16_t data_segs = (data_len + FS_SEGMENT_SIZE - 1u) / FS_SEGMENT_SIZE;
    return 1u + data_segs + 1u;  /* header + data + EOF */
}

/* ── Public API ─────────────────────────────────────────────────── */

void fs_init(void) {
    /* Nothing to do beyond what FRAM already handles —
     * index table is read directly from FRAM on every operation.
     * A fresh/unformatted tertiary partition will have 0xFF bytes
     * everywhere; 0xFFFF is our FS_SLOT_EMPTY sentinel, so it's safe. */
}

void fs_format(void) {
    /* Zero the entire index table */
    uint8_t zeros[FS_INDEX_ENTRY_SZ];
    memset(zeros, 0xFF, sizeof(zeros));  /* 0xFFFF = empty for each entry */

    for (uint8_t i = 0; i < FS_MAX_BLOCKS; i++) {
        uint16_t base = FS_INDEX_BASE + (uint16_t)i * FS_INDEX_ENTRY_SZ;
        fram_write_buf(base, zeros, FS_INDEX_ENTRY_SZ);
    }

    /* Zero storage area in 128-byte chunks */
    uint8_t zbuf[128];
    memset(zbuf, 0x00, sizeof(zbuf));
    uint16_t addr      = FS_STORAGE_BASE;
    uint16_t remaining = FS_STORAGE_SIZE;
    while (remaining) {
        uint16_t chunk = (remaining > 128u) ? 128u : remaining;
        fram_write_buf(addr, zbuf, chunk);
        addr      += chunk;
        remaining -= chunk;
    }
}

fs_status_t fs_write(uint8_t block, uint8_t type,
                     const char *name,
                     const void *data, uint16_t len) {

    if (block >= FS_MAX_BLOCKS)            return FS_ERR_BADBLOCK;
    if (type != FS_TYPE_TEXT &&
        type != FS_TYPE_INT)               return FS_ERR_BADTYPE;

    /* If slot occupied, delete first (defrag runs inside fs_delete) */
    {
        uint16_t addr, size;
        idx_read(block, &addr, &size);
        if (addr != FS_SLOT_EMPTY) {
            fs_status_t st = fs_delete(block);
            if (st != FS_OK) return st;
        }
    }

    /* Compute size and check it fits */
    uint16_t total_segs  = segments_for(len);
    uint16_t total_bytes = total_segs * FS_SEGMENT_SIZE;

    if (storage_used() + total_bytes > FS_STORAGE_SIZE) return FS_ERR_BOUNDS;

    /* Find write address: end of last occupied block */
    uint16_t write_addr = FS_STORAGE_BASE;
    for (uint8_t i = 0; i < FS_MAX_BLOCKS; i++) {
        uint16_t addr, size;
        idx_read(i, &addr, &size);
        if (addr != FS_SLOT_EMPTY) {
            uint16_t end = addr + size;
            if (end > write_addr) write_addr = end;
        }
    }

    /* ── Segment 0: header ────────────────────────────────────────── */
    uint8_t hdr[FS_SEGMENT_SIZE];
    memset(hdr, 0x00, FS_SEGMENT_SIZE);
    hdr[0] = FS_MAGIC_0;
    hdr[1] = FS_MAGIC_1;
    hdr[2] = type;
    /* Copy name, max 12 chars, rest stays 0x00 */
    if (name) {
        uint8_t nlen = 0;
        while (nlen < FS_NAME_MAX && name[nlen]) nlen++;
        memcpy(&hdr[3], name, nlen);
    }
    /* hdr[15] = 0x00 reserved */
    fram_write_buf(write_addr, hdr, FS_SEGMENT_SIZE);
    write_addr += FS_SEGMENT_SIZE;

    /* ── Segments 1..N: data ──────────────────────────────────────── */
    const uint8_t *src        = (const uint8_t *)data;
    uint16_t       written    = 0;
    uint16_t       data_segs  = total_segs - 2u;  /* exclude header + EOF */

    for (uint16_t s = 0; s < data_segs; s++) {
        uint8_t seg[FS_SEGMENT_SIZE];
        memset(seg, 0x00, FS_SEGMENT_SIZE);
        for (uint8_t b = 0; b < FS_SEGMENT_SIZE; b++) {
            if (written < len) {
                seg[b] = src[written++];
            } else {
                seg[b] = 0x00;  /* zero-pad if len overshoots actual data */
            }
        }
        fram_write_buf(write_addr, seg, FS_SEGMENT_SIZE);
        write_addr += FS_SEGMENT_SIZE;
    }

    /* ── EOF marker segment ───────────────────────────────────────── */
    uint8_t eof_seg[FS_SEGMENT_SIZE];
    memset(eof_seg, 0x00, FS_SEGMENT_SIZE);
    fram_write_buf(write_addr, eof_seg, FS_SEGMENT_SIZE);

    /* ── Update index ─────────────────────────────────────────────── */
    /* write_addr now points PAST the EOF segment, so block start was: */
    uint16_t block_start = write_addr - total_bytes;
    idx_write(block, block_start, total_bytes);

    return FS_OK;
}

fs_status_t fs_read_open(uint8_t block, fs_cursor_t *cursor) {
    if (block >= FS_MAX_BLOCKS) return FS_ERR_BADBLOCK;

    uint16_t addr, size;
    idx_read(block, &addr, &size);
    if (addr == FS_SLOT_EMPTY) return FS_ERR_EMPTY;

    cursor->addr = addr;
    cursor->eof  = false;
    return FS_OK;
}

fs_status_t fs_read_next(fs_cursor_t *cursor, uint8_t buf[FS_SEGMENT_SIZE]) {
    if (cursor->eof) {
        memset(buf, 0x00, FS_SEGMENT_SIZE);
        return FS_OK;
    }

    fram_read_buf(cursor->addr, buf, FS_SEGMENT_SIZE);

    /* Check if this segment is the EOF marker (all zeroes) */
    bool is_eof = true;
    for (uint8_t i = 0; i < FS_SEGMENT_SIZE; i++) {
        if (buf[i] != 0x00) { is_eof = false; break; }
    }

    cursor->addr += FS_SEGMENT_SIZE;
    if (is_eof) cursor->eof = true;

    return FS_OK;
}

fs_status_t fs_delete(uint8_t block) {
    if (block >= FS_MAX_BLOCKS) return FS_ERR_BADBLOCK;

    uint16_t addr, size;
    idx_read(block, &addr, &size);
    if (addr == FS_SLOT_EMPTY) return FS_ERR_EMPTY;

    idx_clear(block);
    fs_defrag();
    return FS_OK;
}

bool fs_exists(uint8_t block) {
    if (block >= FS_MAX_BLOCKS) return false;
    uint16_t addr, size;
    idx_read(block, &addr, &size);
    return (addr != FS_SLOT_EMPTY);
}

uint16_t fs_free_bytes(void) {
    uint16_t used = storage_used();
    return (used < FS_STORAGE_SIZE) ? (FS_STORAGE_SIZE - used) : 0u;
}

uint8_t fs_block_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < FS_MAX_BLOCKS; i++) {
        uint16_t addr, size;
        idx_read(i, &addr, &size);
        if (addr != FS_SLOT_EMPTY) count++;
    }
    return count;
}

/* ── Defragmentation ────────────────────────────────────────────── */
/*
 * Strategy: build a sorted list of all occupied blocks by their FRAM address,
 * then repack them contiguously from FS_STORAGE_BASE, updating the index.
 * Uses a small on-stack scratch buffer (FS_SEGMENT_SIZE bytes) to move
 * data one segment at a time — no heap, no large stack allocation.
 */

/* Simple insertion sort on address field — in-place, max 128 entries */
typedef struct { uint8_t block; uint16_t addr; uint16_t size; } block_rec_t;

static void sort_by_addr(block_rec_t *arr, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {
        block_rec_t key = arr[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && arr[j].addr > key.addr) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

void fs_defrag(void) {
    /* Collect all occupied blocks */
    block_rec_t recs[FS_MAX_BLOCKS];
    uint8_t     count = 0;

    for (uint8_t i = 0; i < FS_MAX_BLOCKS; i++) {
        uint16_t addr, size;
        idx_read(i, &addr, &size);
        if (addr != FS_SLOT_EMPTY) {
            recs[count].block = i;
            recs[count].addr  = addr;
            recs[count].size  = size;
            count++;
        }
    }

    if (count == 0) return;

    sort_by_addr(recs, count);

    /* Move each block to its compacted destination */
    uint16_t dest = FS_STORAGE_BASE;
    uint8_t  seg_buf[FS_SEGMENT_SIZE];

    for (uint8_t i = 0; i < count; i++) {
        uint16_t src  = recs[i].addr;
        uint16_t size = recs[i].size;

        if (src != dest) {
            /* Move block one segment at a time */
            uint16_t moved = 0;
            while (moved < size) {
                fram_read_buf(src + moved, seg_buf, FS_SEGMENT_SIZE);
                fram_write_buf(dest + moved, seg_buf, FS_SEGMENT_SIZE);
                moved += FS_SEGMENT_SIZE;
            }
            /* Update index to new address */
            idx_write(recs[i].block, dest, size);
        }

        dest += size;
    }

    /* Zero out the now-free tail */
    uint16_t tail_start = dest;
    uint16_t tail_len   = (uint16_t)(FS_STORAGE_END - tail_start + 1u);
    uint8_t  zeros[FS_SEGMENT_SIZE];
    memset(zeros, 0x00, FS_SEGMENT_SIZE);
    while (tail_len >= FS_SEGMENT_SIZE) {
        fram_write_buf(tail_start, zeros, FS_SEGMENT_SIZE);
        tail_start += FS_SEGMENT_SIZE;
        tail_len   -= FS_SEGMENT_SIZE;
    }
    if (tail_len > 0) {
        fram_write_buf(tail_start, zeros, tail_len);
    }
}
