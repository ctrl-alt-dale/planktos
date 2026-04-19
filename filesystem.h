/* [TYPE: HEADER]
 * filesystem.h | PlanktOS block filesystem over FRAM tertiary partition
 *
 *  Layout inside TERTIARY PARTITION (0x1C00–0x7FFF, ~25 KB):
 *
 *  INDEX TABLE   0x1C00   0x1DFF   512 bytes
 *    128 entries × 4 bytes
 *    [0–1] FRAM address of block's first segment (0xFFFF = empty)
 *    [2–3] total block size in bytes
 *
 *  FILE STORAGE  0x1E00   0x7FFF
 *    Each file is a chain of fixed 16-byte segments:
 *
 *    Segment 0  — header
 *      [0–1]  magic    0xF5 0x1E
 *      [2]    type     FS_TYPE_TEXT (0x01) | FS_TYPE_INT (0x02)
 *      [3–14] name     12 bytes, null-padded
 *      [15]   reserved 0x00
 *
 *    Segments 1..N — data (16 bytes each, zero-padded to segment boundary)
 *    Segment N+1   — EOF marker (16 × 0x00)
 *
 *  Max blocks     : 128 logical slots; actual capacity shrinks as data grows
 *  Max file size  : limited to remaining tertiary space (≤ ~25 KB − index)
 *  Defragmentation: automatic after every write/delete
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ── Constants ─────────────────────────────────────────────────── */

#define FS_SEGMENT_SIZE     16u
#define FS_NAME_MAX         12u
#define FS_MAX_BLOCKS       128u

#define FS_MAGIC_0          0xF5u
#define FS_MAGIC_1          0x1Eu

#define FS_TYPE_TEXT        0x01u
#define FS_TYPE_INT         0x02u

#define FS_INDEX_BASE       0x1C00u                        /* start of index table   */
#define FS_INDEX_ENTRY_SZ   4u                             /* bytes per index entry  */
#define FS_INDEX_SIZE       (FS_MAX_BLOCKS * FS_INDEX_ENTRY_SZ)  /* 512 bytes        */
#define FS_STORAGE_BASE     (FS_INDEX_BASE + FS_INDEX_SIZE)      /* 0x1E00           */
#define FS_STORAGE_END      0x7FFFu
#define FS_STORAGE_SIZE     (FS_STORAGE_END - FS_STORAGE_BASE + 1u)

#define FS_SLOT_EMPTY       0xFFFFu

/* ── Status codes ───────────────────────────────────────────────── */

typedef enum {
    FS_OK            =  0,
    FS_ERR_BOUNDS    = -1,   /* write would exceed partition         */
    FS_ERR_NOSLOT    = -2,   /* no free index slot available         */
    FS_ERR_BADBLOCK  = -3,   /* block number out of range            */
    FS_ERR_EMPTY     = -4,   /* block slot is not occupied           */
    FS_ERR_BADMAGIC  = -5,   /* header magic mismatch                */
    FS_ERR_BADTYPE   = -6,   /* unknown file type                    */
} fs_status_t;

/* ── Read cursor ────────────────────────────────────────────────── */

typedef struct {
    uint16_t addr;      /* FRAM address of next segment to read      */
    bool     eof;       /* true after EOF marker has been returned   */
} fs_cursor_t;

/* ── C API ──────────────────────────────────────────────────────── */

#ifdef __cplusplus
extern "C" {
#endif

/*  Initialize the filesystem.
 *  Scans the index table; call once in setup() before any FS operation. */
void fs_init(void);

/*  Erase entire filesystem (index + storage).
 *  Does NOT touch primary or secondary partitions. */
void fs_format(void);

/*  Write a file into block slot `block`.
 *  If the slot is occupied it is implicitly deleted first.
 *  `data`  : pointer to raw bytes (text or int payload)
 *  `len`   : number of bytes to consume from `data`
 *            — if len > actual data: remainder written as 0x00
 *            — if len < actual data: excess data ignored
 *  `type`  : FS_TYPE_TEXT or FS_TYPE_INT
 *  `name`  : up to 12 chars, truncated + null-padded automatically */
fs_status_t fs_write(uint8_t block, uint8_t type,
                     const char *name,
                     const void *data, uint16_t len);

/*  Open a block for sequential reading.
 *  First call to fs_read_next() returns the header segment.
 *  Subsequent calls return data segments.
 *  Returns FS_ERR_EMPTY / FS_ERR_BADBLOCK on failure. */
fs_status_t fs_read_open(uint8_t block, fs_cursor_t *cursor);

/*  Read the next 16-byte segment into `buf` (must be ≥ 16 bytes).
 *  Sets cursor->eof = true when the EOF marker segment is returned.
 *  Returns FS_OK, or FS_ERR_BADMAGIC if the block is corrupt. */
fs_status_t fs_read_next(fs_cursor_t *cursor, uint8_t buf[FS_SEGMENT_SIZE]);

/*  Delete block `block` and run defragmentation. */
fs_status_t fs_delete(uint8_t block);

/*  Returns true if block slot `block` is occupied. */
bool fs_exists(uint8_t block);

/*  Returns number of bytes still free in the storage area. */
uint16_t fs_free_bytes(void);

/*  Returns highest occupied block index + 1 (i.e. next logical slot).
 *  Returns 0 if filesystem is empty. */
uint8_t fs_block_count(void);

/*  Internal: compact storage — called automatically after write/delete. */
void fs_defrag(void);

#ifdef __cplusplus
}

/* ── C++ wrapper ────────────────────────────────────────────────── */

class POSFS {
public:
    /*  Initialize filesystem. Call once in setup(). */
    static void begin(void) { fs_init(); }

    /*  Erase all files. */
    static void format(void) { fs_format(); }

    /*  Write text data into block.
     *  Example: filesystem.write("hello", 5, 0, "notes"); */
    static fs_status_t write(const char *text, uint16_t len,
                             uint8_t block, const char *name) {
        return fs_write(block, FS_TYPE_TEXT, name, text, len);
    }

    /*  Write integer array into block.
     *  Example: int vals[3] = {1,2,3};
     *           filesystem.write(vals, 3, 1, "counts"); */
    static fs_status_t write(const int *data, uint16_t count,
                             uint8_t block, const char *name) {
        return fs_write(block, FS_TYPE_INT, name,
                        data, (uint16_t)(count * sizeof(int)));
    }

    /*  Open block for reading. Returns cursor. */
    static fs_status_t read_open(uint8_t block, fs_cursor_t &cursor) {
        return fs_read_open(block, &cursor);
    }

    /*  Read next segment. buf must be FS_SEGMENT_SIZE (16) bytes. */
    static fs_status_t read_next(fs_cursor_t &cursor,
                                 uint8_t buf[FS_SEGMENT_SIZE]) {
        return fs_read_next(&cursor, buf);
    }

    /*  Delete block. */
    static fs_status_t del(uint8_t block) {
        return fs_delete(block);
    }

    static bool        exists(uint8_t block)  { return fs_exists(block);     }
    static uint16_t    freeBytes(void)         { return fs_free_bytes();      }
    static uint8_t     blockCount(void)        { return fs_block_count();     }
};

#endif /* __cplusplus */
