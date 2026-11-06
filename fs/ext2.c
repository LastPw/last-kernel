#include "fs_ext2.h"
#include "advanced_allocator.h"
#include "disk.h"
#include "serial.h"
#include "string.h"

static u8 g_ext2_format_sbuf[1024];
static u8 g_ext2_format_gdbuf[4096];
static u8 g_ext2_format_bbitmap[4096];
static u8 g_ext2_format_ibitmap[4096];
static u8 g_ext2_format_itable[4096];
static u8 g_ext2_format_root_data[1024];
static u8 g_ext2_mount_gd[16u * 4096u];
static u8 g_ext2_dir_buf[4096];
static u8 g_ext2_create_dir_buf[4096];
static u8 g_ext2_bitmap_buf[4096];
static u8 g_ext2_indirect_buf[4096];
static u8 g_ext2_double_buf[4096];
static u8 g_ext2_remove_buf[4096];
static u8 g_ext2_list_buf[4096];
static u8 g_ext2_free_buf[4096];
static u8 g_ext2_read_buf[4096];
static u8 g_ext2_write_buf[4096];
static u8 g_ext2_journal_buf[32768];  /* 8 blocks x 4096 for journal ops */
static char g_ext2_path_name[EXT2_NAME_LEN + 1];
static char g_ext2_leaf_buf[EXT2_NAME_LEN + 1];

/* Forward declarations: ext2_bmap is defined later (block-mapping section) but
   referenced earlier by ext4_journal_replay. */
static u32 ext2_bmap(ext2_fs_t* fs, const struct ext2_inode* inode, u32 block_idx);

/* ===================== Low-level I/O ===================== */

static int ext2_read_blocks(ext2_fs_t* fs, u32 block, u8* buffer, u32 count) {
    u32 lba = fs->start_lba + (block * (fs->block_size / DISK_SECTOR_SIZE));
    u32 sectors = count * (fs->block_size / DISK_SECTOR_SIZE);
    for (u32 i = 0; i < sectors; i++) {
        if (disk_read_sector(lba + i, buffer + (i * DISK_SECTOR_SIZE)) != 0) return -1;
    }
    return 0;
}

static int ext2_write_blocks(ext2_fs_t* fs, u32 block, const u8* buffer, u32 count) {
    u32 lba = fs->start_lba + (block * (fs->block_size / DISK_SECTOR_SIZE));
    u32 sectors = count * (fs->block_size / DISK_SECTOR_SIZE);
    for (u32 i = 0; i < sectors; i++) {
        if (disk_write_sector_fast(lba + i, buffer + (i * DISK_SECTOR_SIZE)) != 0) return -1;
    }
    return 0;
}

/* ===================== Group descriptor helpers ===================== */

static u32 ext2_group_of_ino(ext2_fs_t* fs, u32 ino) {
    return (ino - 1) / fs->inodes_per_group;
}

static struct ext2_group_desc* ext2_group_desc(ext2_fs_t* fs, u32 group) {
    /* gd_count is the number of descriptors actually loaded into g_ext2_mount_gd,
     * which may be smaller than group_count (the loader caps at 16 blocks). Bound
     * against the loaded count so an oversized on-disk group_count cannot drive an
     * out-of-bounds read of the descriptor array. */
    if (group >= fs->gd_count || !fs->group_desc) return 0;
    return &fs->group_desc[group];
}

/* Write the group descriptor back to disk */
static int ext2_write_group_desc(ext2_fs_t* fs) {
    u32 gd_block;
    if (fs->block_size == 1024) gd_block = 2;
    else gd_block = 1;

    u32 gd_bytes = fs->group_count * sizeof(struct ext2_group_desc);
    u32 gd_blocks = (gd_bytes + fs->block_size - 1) / fs->block_size;
    if (gd_blocks > 16) gd_blocks = 16;

    for (u32 i = 0; i < gd_blocks; i++) {
        if (ext2_write_blocks(fs, gd_block + i, ((u8*)fs->group_desc) + (i * fs->block_size), 1) != 0)
            return -1;
    }
    return 0;
}

/* ===================== Inode operations ===================== */

static int ext2_read_inode(ext2_fs_t* fs, u32 ino, struct ext2_inode* out) {
    if (ino == 0 || ino > fs->inodes_count) return -1;
    u32 group = ext2_group_of_ino(fs, ino);
    struct ext2_group_desc* gd = ext2_group_desc(fs, group);
    if (!gd) return -1;
    u32 inode_table_block = gd->bg_inode_table;
    u32 offset_in_group = (ino - 1) - (group * fs->inodes_per_group);
    u32 block_offset = offset_in_group / fs->inodes_per_block;
    u32 offset_in_block = (offset_in_group % fs->inodes_per_block) * fs->inode_size;
    u8* buf = g_ext2_dir_buf;
    if (ext2_read_blocks(fs, inode_table_block + block_offset, buf, 1) != 0) return -1;
    memcpy(out, buf + offset_in_block, sizeof(struct ext2_inode));
    return 0;
}

static int ext2_write_inode(ext2_fs_t* fs, u32 ino, const struct ext2_inode* inode) {
    if (ino == 0 || ino > fs->inodes_count) return -1;
    u32 group = ext2_group_of_ino(fs, ino);
    struct ext2_group_desc* gd = ext2_group_desc(fs, group);
    if (!gd) return -1;
    u32 inode_table_block = gd->bg_inode_table;
    u32 offset_in_group = (ino - 1) - (group * fs->inodes_per_group);
    u32 block_offset = offset_in_group / fs->inodes_per_block;
    u32 offset_in_block = (offset_in_group % fs->inodes_per_block) * fs->inode_size;
    u8* buf = g_ext2_dir_buf;
    if (ext2_read_blocks(fs, inode_table_block + block_offset, buf, 1) != 0) return -1;
    memcpy(buf + offset_in_block, inode, sizeof(struct ext2_inode));
    return ext2_write_blocks(fs, inode_table_block + block_offset, buf, 1);
}

/* ===================== Block bitmap operations ===================== */

/* Read block bitmap for a group, find free bit, set it, write back */
static u32 ext2_alloc_block(ext2_fs_t* fs, u32 pref_group) {
    /* Search from pref_group, wrap around */
    for (u32 g = 0; g < fs->group_count; g++) {
        u32 group = (pref_group + g) % fs->group_count;
        struct ext2_group_desc* gd = ext2_group_desc(fs, group);
        if (!gd || gd->bg_free_blocks_count == 0) continue;

        u8* bitmap = g_ext2_bitmap_buf;
        u32 bitmap_block = gd->bg_block_bitmap;
        if (ext2_read_blocks(fs, bitmap_block, bitmap, 1) != 0) continue;

        u32 bits = fs->blocks_per_group;
        if (bits > fs->block_size * 8u) bits = fs->block_size * 8u;  /* bitmap is one block */
        for (u32 i = 0; i < bits; i++) {
            if ((bitmap[i / 8] & (1u << (i % 8))) == 0) {
                bitmap[i / 8] |= (u8)(1u << (i % 8));
                if (ext2_write_blocks(fs, bitmap_block, bitmap, 1) != 0) return 0;
                gd->bg_free_blocks_count--;
                ext2_write_group_desc(fs);
                return group * fs->blocks_per_group + i + fs->first_data_block;
            }
        }
    }
    serial_write("[LastOS] ext2 alloc block exhausted groups=");
    serial_write_dec(fs->group_count);
    serial_write(" free0=");
    if (fs->group_count != 0u && fs->group_desc) serial_write_dec(fs->group_desc[0].bg_free_blocks_count);
    serial_writeln("");
    return 0;
}

static void ext2_free_block(ext2_fs_t* fs, u32 block) {
    if (block < fs->first_data_block) return;
    u32 group = (block - fs->first_data_block) / fs->blocks_per_group;
    u32 bit = (block - fs->first_data_block) % fs->blocks_per_group;
    struct ext2_group_desc* gd = ext2_group_desc(fs, group);
    if (!gd) return;

    u8* bitmap = g_ext2_bitmap_buf;
    u32 bitmap_block = gd->bg_block_bitmap;
    if (ext2_read_blocks(fs, bitmap_block, bitmap, 1) != 0) return;
    bitmap[bit / 8] &= (u8)~(1u << (bit % 8));
    ext2_write_blocks(fs, bitmap_block, bitmap, 1);
    gd->bg_free_blocks_count++;
    ext2_write_group_desc(fs);
}

/* ===================== Inode bitmap operations ===================== */

static u32 ext2_alloc_inode(ext2_fs_t* fs) {
    for (u32 g = 0; g < fs->group_count; g++) {
        struct ext2_group_desc* gd = ext2_group_desc(fs, g);
        if (!gd || gd->bg_free_inodes_count == 0) continue;

        u8* bitmap = g_ext2_bitmap_buf;
        u32 bitmap_block = gd->bg_inode_bitmap;
        if (ext2_read_blocks(fs, bitmap_block, bitmap, 1) != 0) continue;

        u32 ibits = fs->inodes_per_group;
        if (ibits > fs->block_size * 8u) ibits = fs->block_size * 8u;  /* bitmap is one block */
        for (u32 i = 0; i < ibits; i++) {
            if ((bitmap[i / 8] & (1u << (i % 8))) == 0) {
                bitmap[i / 8] |= (u8)(1u << (i % 8));
                if (ext2_write_blocks(fs, bitmap_block, bitmap, 1) != 0) return 0;
                gd->bg_free_inodes_count--;
                ext2_write_group_desc(fs);
                return g * fs->inodes_per_group + i + 1;
            }
        }
    }
    return 0;
}

static void ext2_free_inode(ext2_fs_t* fs, u32 ino) {
    if (ino == 0 || ino > fs->inodes_count) return;
    u32 group = (ino - 1) / fs->inodes_per_group;
    u32 bit = (ino - 1) % fs->inodes_per_group;
    struct ext2_group_desc* gd = ext2_group_desc(fs, group);
    if (!gd) return;

    u8* bitmap = g_ext2_bitmap_buf;
    u32 bitmap_block = gd->bg_inode_bitmap;
    if (ext2_read_blocks(fs, bitmap_block, bitmap, 1) != 0) return;
    bitmap[bit / 8] &= (u8)~(1u << (bit % 8));
    ext2_write_blocks(fs, bitmap_block, bitmap, 1);
    gd->bg_free_inodes_count++;
    ext2_write_group_desc(fs);
}

/* ===================== Ext4 Extent Tree Block Mapping ===================== */

/* The extent tree root lives in the inode's i_block[] area (first 60 bytes). */
#define EXT4_EXTENT_ROOT_BYTES 60u

/*
 * Clamp an on-disk eh_entries count to the number of entries that physically
 * fit in the node buffer. eh_entries is attacker-controlled (up to 65535) and
 * must never be trusted to bound the binary-search index, otherwise the search
 * reads far past the 60-byte inode root or the 4096-byte node buffer.
 */
static u16 ext4_clamp_entries(u16 entries, u32 node_bytes, u32 entry_size) {
    u32 cap;
    if (node_bytes <= sizeof(struct ext4_extent_header)) return 0;
    cap = (node_bytes - (u32)sizeof(struct ext4_extent_header)) / entry_size;
    return (entries > cap) ? (u16)cap : entries;
}

/*
 * Walk the ext4 extent tree to map a logical block to a physical block.
 * The extent tree root is stored in i_block[] (first 60 bytes).
 * Supports depth 0 (leaf directly in inode) and depth >=1 (index nodes).
 */
static u32 ext4_extent_bmap(ext2_fs_t* fs, const struct ext2_inode* inode, u32 block_idx) {
    struct ext4_extent_header* eh;
    /* Use the indirect block buffer (4096 bytes max) for extent tree nodes.
       Safe: extent tree walking completes before any caller reuses this buffer. */
    u8* node_buf = g_ext2_indirect_buf;

    /* Extent root is within i_block[0..14] (first 60 bytes) */
    eh = (struct ext4_extent_header*)inode->i_block;
    if (eh->eh_magic != 0xF30Au) return 0;  /* Not a valid extent tree */

    if (eh->eh_depth == 0) {
        /* Leaf node: search extent entries directly in the inode */
        struct ext4_extent* ext = (struct ext4_extent*)(eh + 1);
        /* eh_entries is attacker-controlled: clamp to what fits in the 60-byte root */
        u16 entries = ext4_clamp_entries(eh->eh_entries, EXT4_EXTENT_ROOT_BYTES, sizeof(struct ext4_extent));
        /* Binary search for the extent covering block_idx */
        i32 lo = 0, hi = (i32)entries - 1;
        while (lo <= hi) {
            i32 mid = (lo + hi) / 2;
            u32 eb = ext[mid].ee_block;
            u16 el = ext[mid].ee_len;
            if (block_idx < eb) {
                hi = mid - 1;
            } else if (block_idx >= eb + el) {
                lo = mid + 1;
            } else {
                /* Found: return physical block */
                u32 phys = ext[mid].ee_start_lo | ((u32)ext[mid].ee_start_hi << 16u);
                return phys + (block_idx - eb);
            }
        }
        return 0; /* Not found */
    }

    /* Depth > 0: walk index nodes. Start from the root. */
    {
        u8 depth = eh->eh_depth;
        u16 entries = ext4_clamp_entries(eh->eh_entries, EXT4_EXTENT_ROOT_BYTES, sizeof(struct ext4_extent_idx));
        struct ext4_extent_idx* idx = (struct ext4_extent_idx*)(eh + 1);
        u32 child_block = 0;
        i32 lo, hi, mid;
        u8* curr_node = node_buf;
        int is_root = 1;

        while (depth > 0) {
            if (is_root) {
                /* Search the root index node (in inode) */
                lo = 0; hi = (i32)entries - 1;
                child_block = 0;
                while (lo <= hi) {
                    mid = (lo + hi) / 2;
                    if (block_idx < idx[mid].ei_block) {
                        hi = mid - 1;
                    } else {
                        child_block = idx[mid].ei_leaf_lo | ((u32)idx[mid].ei_leaf_hi << 16u);
                        lo = mid + 1;
                    }
                }
                if (child_block == 0) return 0;
                is_root = 0;
            } else {
                /* Read index node block from disk */
                if (ext2_read_blocks(fs, child_block, curr_node, 1) != 0) return 0;
                eh = (struct ext4_extent_header*)curr_node;
                if (eh->eh_magic != 0xF30Au) return 0;
                entries = ext4_clamp_entries(eh->eh_entries, fs->block_size, sizeof(struct ext4_extent_idx));
                idx = (struct ext4_extent_idx*)(eh + 1);
                lo = 0; hi = (i32)entries - 1;
                child_block = 0;
                while (lo <= hi) {
                    mid = (lo + hi) / 2;
                    if (block_idx < idx[mid].ei_block) {
                        hi = mid - 1;
                    } else {
                        child_block = idx[mid].ei_leaf_lo | ((u32)idx[mid].ei_leaf_hi << 16u);
                        lo = mid + 1;
                    }
                }
                if (child_block == 0) return 0;
            }
            depth--;
        }

        /* Read the final leaf node block */
        if (ext2_read_blocks(fs, child_block, curr_node, 1) != 0) return 0;
        eh = (struct ext4_extent_header*)curr_node;
        if (eh->eh_magic != 0xF30Au) return 0;
        {
            struct ext4_extent* ext = (struct ext4_extent*)(eh + 1);
            entries = ext4_clamp_entries(eh->eh_entries, fs->block_size, sizeof(struct ext4_extent));
            lo = 0; hi = (i32)entries - 1;
            while (lo <= hi) {
                mid = (lo + hi) / 2;
                u32 eb = ext[mid].ee_block;
                u16 el = ext[mid].ee_len;
                if (block_idx < eb) {
                    hi = mid - 1;
                } else if (block_idx >= eb + el) {
                    lo = mid + 1;
                } else {
                    u32 phys = ext[mid].ee_start_lo | ((u32)ext[mid].ee_start_hi << 16u);
                    return phys + (block_idx - eb);
                }
            }
        }
    }
    return 0;
}

/* ===================== Ext4 Journal Replay ===================== */

/*
 * Replay the ext4 (JBD2) journal so the filesystem metadata is consistent.
 * This is needed before mounting any filesystem that had a journal active
 * (i.e. was not cleanly unmounted).
 *
 * Algorithm:
 *   1. Read the journal inode to locate journal blocks
 *   2. Read the journal superblock to find start/sequence
 *   3. Walk transactions: [Descriptor] [data blocks...] [Commit]
 *   4. For each descriptor tag, copy the data block to its proper location
 *   5. Continue until the journal is clean
 *
 * Returns 0 on success (journal replayed or empty), -1 on error.
 */
static int ext4_journal_replay(ext2_fs_t* fs) {
    struct ext2_super_block sb;
    u8* sbuf = g_ext2_journal_buf;
    u8* data_blk = g_ext2_journal_buf + 4096;

    /* Read superblock to find journal inode */
    {
        u32 sb_lba = fs->start_lba + 2;
        for (u32 i = 0; i < 2; i++) {
            if (disk_read_sector(sb_lba + i, sbuf + (i * DISK_SECTOR_SIZE)) != 0) {
                serial_writeln("[LastOS] ext4 journal: superblock read failed");
                return -1;
            }
        }
        memcpy(&sb, sbuf, sizeof(sb));
    }

    /* Check if a journal inode exists */
    if (sb.s_journal_inum == 0) {
        serial_writeln("[LastOS] ext4: no journal inode, skipping recovery");
        return 0;  /* No journal, nothing to replay */
    }

    /* Read the journal inode */
    struct ext2_inode jinode;
    if (ext2_read_inode(fs, sb.s_journal_inum, &jinode) != 0) {
        serial_writeln("[LastOS] ext4 journal: cannot read journal inode");
        return -1;
    }

    u32 journal_size = jinode.i_size;
    u32 journal_block_size = fs->block_size;
    u32 journal_blocks = journal_size / journal_block_size;

    if (journal_blocks < 8 || journal_blocks > 32768) {
        serial_writeln("[LastOS] ext4 journal: invalid size");
        return -1;
    }

    serial_write("[LastOS] ext4 journal: ");
    serial_write_dec(journal_blocks);
    serial_writeln(" blocks");

    /* Read the journal superblock (block 0 of journal) */
    {
        u32 j_block0 = ext2_bmap(fs, &jinode, 0);
        if (j_block0 == 0) {
            serial_writeln("[LastOS] ext4 journal: cannot read block 0");
            return -1;
        }
        if (ext2_read_blocks(fs, j_block0, sbuf, 1) != 0) {
            serial_writeln("[LastOS] ext4 journal: superblock read failed");
            return -1;
        }
    }

    struct jbd2_superblock_s* jsb = (struct jbd2_superblock_s*)sbuf;
    if (jsb->s_header != JBD2_MAGIC_NUMBER) {
        serial_writeln("[LastOS] ext4 journal: bad magic (not JBD2)");
        return -1;
    }

    u32 log_start = jsb->s_start;        /* First block of the log */
    u32 log_first = jsb->s_first;         /* First block in journal area */
    u32 log_maxlen = jsb->s_maxlen;       /* Total journal blocks */
    u32 log_sequence = jsb->s_sequence;   /* Next expected sequence number */

    if (log_start == 0) {
        serial_writeln("[LastOS] ext4 journal: clean (no transactions)");
        return 0;  /* Clean journal */
    }

    if (log_first >= journal_blocks || log_maxlen > journal_blocks || log_maxlen < 8) {
        serial_writeln("[LastOS] ext4 journal: invalid layout");
        return -1;
    }

    serial_write("[LastOS] ext4 journal: replay start=");
    serial_write_dec(log_start);
    serial_write(" first=");
    serial_write_dec(log_first);
    serial_writeln("");

    /* Walk through the journal replaying transactions */
    {
        u32 cur = log_start;
        u32 seq = log_sequence;
        u32 tx_count = 0;
        u32 block_replayed = 0;

        while (tx_count < JBD2_MAX_TRANSACTIONS) {
            /* Read the current journal block */
            u32 j_block = ext2_bmap(fs, &jinode, cur);
            if (j_block == 0) break;
            if (ext2_read_blocks(fs, j_block, sbuf, 1) != 0) break;

            struct jbd2_header_s* hdr = (struct jbd2_header_s*)sbuf;
            if (hdr->h_magic != JBD2_MAGIC_NUMBER) {
                serial_writeln("[LastOS] ext4 journal: bad block magic, stopping replay");
                break;
            }

            if (hdr->h_blocktype == JBD2_DESCRIPTOR_BLOCK) {
                /* This is a descriptor block: contains tags for metadata blocks */
                u32 desc_seq = hdr->h_sequence;
                u32 tags_per_block = (journal_block_size - sizeof(struct jbd2_header_s)) / sizeof(struct jbd2_tag_s);
                struct jbd2_tag_s* tags = (struct jbd2_tag_s*)(sbuf + sizeof(struct jbd2_header_s));
                u32 cur_pos = cur;

                /* Advance past descriptor block */
                cur_pos = (cur_pos + 1) % log_maxlen;
                if (cur_pos == 0) cur_pos = log_first;

                /* Enable ESCAPE decoding for the journal */
                /* Iterate through tags */
                for (u32 ti = 0; ti < tags_per_block; ti++) {
                    u32 tag_blocknr = tags[ti].t_blocknr;
                    u32 tag_flags = tags[ti].t_flags;

                    if (tag_blocknr == 0) {
                        if (tag_flags & JBD2_FLAG_LAST_TAG) break;  /* End of tags */
                        continue;
                    }

                    /* Read the data block from the journal */
                    u32 data_jblock = ext2_bmap(fs, &jinode, cur_pos);
                    if (data_jblock == 0) {
                        serial_writeln("[LastOS] ext4 journal: cannot read data block");
                        break;
                    }
                    if (ext2_read_blocks(fs, data_jblock, data_blk, 1) != 0) break;

                    /* Handle ESCAPED blocks: data is XOR'd with 0xFFFFFFFF */
                    if (tag_flags & JBD2_FLAG_ESCAPE) {
                        u32* dp = (u32*)data_blk;
                        u32 words = fs->block_size / 4;
                        for (u32 wi = 0; wi < words; wi++) {
                            dp[wi] = ~dp[wi];
                        }
                    }

                    /* Copy data to the proper filesystem block. t_blocknr comes
                     * from the (untrusted) on-disk journal; reject any target
                     * outside this filesystem so a crafted journal cannot scribble
                     * over the MBR/GPT, other partitions, or metadata. */
                    u32 fs_block = tag_blocknr;
                    if (fs_block >= fs->blocks_count) {
                        serial_writeln("[LastOS] ext4 journal: tag block out of range, skipping");
                        cur_pos = (cur_pos + 1) % log_maxlen;
                        if (cur_pos == 0) cur_pos = log_first;
                        if (tag_flags & JBD2_FLAG_LAST_TAG) break;
                        continue;
                    }
                    if (ext2_write_blocks(fs, fs_block, data_blk, 1) != 0) {
                        serial_writeln("[LastOS] ext4 journal: write block failed!");
                        break;
                    }
                    block_replayed++;

                    /* Advance to next data block */
                    cur_pos = (cur_pos + 1) % log_maxlen;
                    if (cur_pos == 0) cur_pos = log_first;

                    if (tag_flags & JBD2_FLAG_LAST_TAG) break;
                }

                /* Now find the commit block */
                {
                    u32 commit_pos = cur_pos;
                    /* The commit block should be right after the last data block */
                    u32 commit_jblock = ext2_bmap(fs, &jinode, commit_pos);
                    if (commit_jblock == 0) break;
                    if (ext2_read_blocks(fs, commit_jblock, sbuf, 1) != 0) break;

                    struct jbd2_header_s* cmt = (struct jbd2_header_s*)sbuf;
                    if (cmt->h_magic == JBD2_MAGIC_NUMBER && cmt->h_blocktype == JBD2_COMMIT_BLOCK) {
                        /* Verify the commit block's sequence number */
                        struct jbd2_commit_tail_s* tail;
                        u32 tail_off = journal_block_size - sizeof(struct jbd2_commit_tail_s);
                        tail = (struct jbd2_commit_tail_s*)(sbuf + tail_off);
                        if (tail->t_magic == JBD2_MAGIC_NUMBER && tail->t_sequence == desc_seq) {
                            serial_write("[LastOS] ext4 journal: committed tx ");
                            serial_write_dec(desc_seq);
                            serial_writeln("");
                        } else {
                            serial_writeln("[LastOS] ext4 journal: commit block mismatch, may be partial");
                            break;
                        }
                    } else {
                        serial_writeln("[LastOS] ext4 journal: missing commit block, partial transaction");
                        break;
                    }

                    /* Skip past the commit block */
                    cur_pos = (commit_pos + 1) % log_maxlen;
                    if (cur_pos == 0) cur_pos = log_first;
                    cur = cur_pos;
                }

                tx_count++;
                seq++;

            } else if (hdr->h_blocktype == JBD2_SUPERBLOCK_V2) {
                /* Journal superblock - skip, already handled */
                break;
            } else {
                /* Unknown block type or revoke block - stop replay */
                serial_write("[LastOS] ext4 journal: unexpected block type ");
                serial_write_dec(hdr->h_blocktype);
                serial_writeln(", stopping");
                break;
            }

            /* Safety: wrap around */
            if (cur == log_start) break;
        }

        serial_write("[LastOS] ext4 journal: replay complete, ");
        serial_write_dec(tx_count);
        serial_write(" transactions, ");
        serial_write_dec(block_replayed);
        serial_writeln(" metadata blocks");
    }

    serial_writeln("[LastOS] ext4 journal: recovery successful");
    return 0;
}

/* ===================== Block mapping (bmap) ===================== */

static u32 ext2_bmap(ext2_fs_t* fs, const struct ext2_inode* inode, u32 block_idx) {
    /* If ext4 extents are in use, delegate to the extent tree walker */
    if (fs->use_extents) {
        return ext4_extent_bmap(fs, inode, block_idx);
    }

    u32 addr_per_block = fs->block_size / 4;
    if (block_idx < EXT2_NDIR_BLOCKS) return inode->i_block[block_idx];
    block_idx -= EXT2_NDIR_BLOCKS;
    if (block_idx < addr_per_block) {
        if (inode->i_block[EXT2_IND_BLOCK] == 0) return 0;
        u8* buf = g_ext2_indirect_buf;
        if (ext2_read_blocks(fs, inode->i_block[EXT2_IND_BLOCK], buf, 1) != 0) return 0;
        return ((u32*)buf)[block_idx];
    }
    block_idx -= addr_per_block;
    if (block_idx >= addr_per_block * addr_per_block || inode->i_block[EXT2_DIND_BLOCK] == 0) return 0;
    u8* root = g_ext2_indirect_buf;
    u8* leaf = g_ext2_double_buf;
    u32 root_idx = block_idx / addr_per_block;
    u32 leaf_idx = block_idx % addr_per_block;
    if (ext2_read_blocks(fs, inode->i_block[EXT2_DIND_BLOCK], root, 1) != 0) return 0;
    if (((u32*)root)[root_idx] == 0) return 0;
    if (ext2_read_blocks(fs, ((u32*)root)[root_idx], leaf, 1) != 0) return 0;
    return ((u32*)leaf)[leaf_idx];
}

/* Set a block pointer for an inode at the given logical block index.
   Allocates indirect blocks as needed. Returns physical block number or 0. */
static u32 ext2_set_bmap(ext2_fs_t* fs, struct ext2_inode* inode, u32 block_idx, u32 phys_block) {
    u32 addr_per_block = fs->block_size / 4;

    if (block_idx < EXT2_NDIR_BLOCKS) {
        inode->i_block[block_idx] = phys_block;
        return phys_block;
    }

    block_idx -= EXT2_NDIR_BLOCKS;

    if (block_idx < addr_per_block) {
        /* Need indirect block */
        if (inode->i_block[EXT2_IND_BLOCK] == 0) {
            u32 ind = ext2_alloc_block(fs, 0);
            if (ind == 0) return 0;
            inode->i_block[EXT2_IND_BLOCK] = ind;
        }
        u8* buf = g_ext2_indirect_buf;
        memset(buf, 0, sizeof(g_ext2_indirect_buf));
        if (phys_block != 0) {
            /* Read existing if present */
            ext2_read_blocks(fs, inode->i_block[EXT2_IND_BLOCK], buf, 1);
        }
        ((u32*)buf)[block_idx] = phys_block;
        if (ext2_write_blocks(fs, inode->i_block[EXT2_IND_BLOCK], buf, 1) != 0) return 0;
        return phys_block;
    }
    block_idx -= addr_per_block;
    if (block_idx >= addr_per_block * addr_per_block) return 0; /* Triple indirect not supported */

    u8* root = g_ext2_indirect_buf;
    u8* leaf = g_ext2_double_buf;
    u32 root_idx = block_idx / addr_per_block;
    u32 leaf_idx = block_idx % addr_per_block;
    if (inode->i_block[EXT2_DIND_BLOCK] == 0) {
        u32 dind = ext2_alloc_block(fs, 0);
        if (dind == 0) return 0;
        memset(root, 0, fs->block_size);
        if (ext2_write_blocks(fs, dind, root, 1) != 0) { ext2_free_block(fs, dind); return 0; }
        inode->i_block[EXT2_DIND_BLOCK] = dind;
    } else if (ext2_read_blocks(fs, inode->i_block[EXT2_DIND_BLOCK], root, 1) != 0) {
        return 0;
    }

    u32 leaf_block = ((u32*)root)[root_idx];
    if (leaf_block == 0) {
        leaf_block = ext2_alloc_block(fs, 0);
        if (leaf_block == 0) return 0;
        memset(leaf, 0, fs->block_size);
        if (ext2_write_blocks(fs, leaf_block, leaf, 1) != 0) { ext2_free_block(fs, leaf_block); return 0; }
        ((u32*)root)[root_idx] = leaf_block;
        if (ext2_write_blocks(fs, inode->i_block[EXT2_DIND_BLOCK], root, 1) != 0) return 0;
    } else if (ext2_read_blocks(fs, leaf_block, leaf, 1) != 0) {
        return 0;
    }
    ((u32*)leaf)[leaf_idx] = phys_block;
    if (ext2_write_blocks(fs, leaf_block, leaf, 1) != 0) return 0;
    return phys_block;
}

/* ===================== Directory operations ===================== */

/* Add a directory entry to a directory */
static int ext2_dir_add_entry(ext2_fs_t* fs, u32 dir_ino, u32 file_ino, const char* name, u8 file_type) {
    struct ext2_inode dir_inode;
    if (ext2_read_inode(fs, dir_ino, &dir_inode) != 0) return -181;
    if (!(dir_inode.i_mode & EXT2_S_IFDIR)) return -182;

    u32 namelen = str_len(name);
    u32 req_len = EXT2_DIR_REC_LEN(namelen);
    u32 size = dir_inode.i_size;
    u8* buf = g_ext2_dir_buf;
    u32 bytes_per_block = fs->block_size;

    /* Scan existing blocks for space */
    for (u32 b = 0; ; b++) {
        u32 phys;
        u8 need_extend = 0;

        if (b * bytes_per_block < size) {
            phys = ext2_bmap(fs, &dir_inode, b);
            if (phys == 0) continue;
        } else {
            /* Need to allocate a new block */
            phys = ext2_alloc_block(fs, 0);
            if (phys == 0) return -183;
            memset(buf, 0, bytes_per_block);
            if (ext2_set_bmap(fs, &dir_inode, b, phys) == 0) { ext2_free_block(fs, phys); return -184; }
            need_extend = 1;

            /* Create a terminating entry that fills the whole block */
            struct ext2_dir_entry_2* de = (struct ext2_dir_entry_2*)buf;
            de->inode = 0;
            de->rec_len = bytes_per_block;
            de->name_len = 0;
            de->file_type = 0;
        }

        if (ext2_read_blocks(fs, phys, buf, 1) != 0) return -185;

        /* Scan for entry we can split or an empty slot */
        u32 off = 0;
        u8 found = 0;
        while (off < bytes_per_block) {
            struct ext2_dir_entry_2* de = (struct ext2_dir_entry_2*)(buf + off);
            u32 rec_len = de->rec_len;
            if (rec_len < 8u || off + rec_len > bytes_per_block) return -186;

            u32 used = de->inode != 0 ? (u32)EXT2_DIR_REC_LEN(de->name_len) : 0;
            if (used > rec_len) return -187;
            u32 slack = rec_len - used;
            if (de->inode != 0) {
                if (slack < req_len) { off += rec_len; continue; }
                /* Reclaim all slack after the existing entry. */
                de->rec_len = (u16)used;
                struct ext2_dir_entry_2* new_de = (struct ext2_dir_entry_2*)(buf + off + used);
                new_de->inode = file_ino;
                new_de->rec_len = (u16)(rec_len - used);
                new_de->name_len = (u8)namelen;
                new_de->file_type = file_type;
                memcpy(new_de->name, name, namelen);
                found = 1;
                break;
            }
            if (rec_len < req_len) { off += rec_len; continue; }
            de->inode = file_ino;
            de->name_len = (u8)namelen;
            de->file_type = file_type;
            memcpy(de->name, name, namelen);
            found = 1;
            break;
            off += rec_len;
        }

        if (!found) {
            if (need_extend) return -188; /* New block should have space */
            continue; /* Try next block */
        }

        if (ext2_write_blocks(fs, phys, buf, 1) != 0) return -189;

        /* Update directory size if we extended */
        if (need_extend) {
            dir_inode.i_size = size + bytes_per_block;
            ext2_write_inode(fs, dir_ino, &dir_inode);
        }
        return 0;
    }
}

/* Remove a directory entry by name */
static int ext2_dir_remove_entry(ext2_fs_t* fs, u32 dir_ino, const char* name) {
    struct ext2_inode dir_inode;
    if (ext2_read_inode(fs, dir_ino, &dir_inode) != 0) return -1;
    if (!(dir_inode.i_mode & EXT2_S_IFDIR)) return -1;

    u32 size = dir_inode.i_size;
    u32 namelen = str_len(name);
    u8* buf = g_ext2_remove_buf;
    u32 bytes_per_block = fs->block_size;

    for (u32 b = 0; b * bytes_per_block < size; b++) {
        u32 phys = ext2_bmap(fs, &dir_inode, b);
        if (phys == 0) continue;
        if (ext2_read_blocks(fs, phys, buf, 1) != 0) continue;

        u32 off = 0;
        while (off + sizeof(struct ext2_dir_entry_2) <= bytes_per_block) {
            struct ext2_dir_entry_2* de = (struct ext2_dir_entry_2*)(buf + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len == namelen &&
                memcmp(de->name, name, namelen) == 0) {
                de->inode = 0;
                return ext2_write_blocks(fs, phys, buf, 1);
            }
            off += de->rec_len;
        }
    }
    return -1;
}

/* Free all blocks belonging to an inode */
static void ext2_free_inode_blocks(ext2_fs_t* fs, struct ext2_inode* inode) {
    u32 addr_per_block = fs->block_size / 4;
    /* Direct blocks */
    for (u32 i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        if (inode->i_block[i]) { ext2_free_block(fs, inode->i_block[i]); inode->i_block[i] = 0; }
    }
    /* Single indirect */
    if (inode->i_block[EXT2_IND_BLOCK]) {
        u8* buf = g_ext2_free_buf;
        if (ext2_read_blocks(fs, inode->i_block[EXT2_IND_BLOCK], buf, 1) == 0) {
            u32* ind = (u32*)buf;
            for (u32 i = 0; i < addr_per_block; i++) {
                if (ind[i]) ext2_free_block(fs, ind[i]);
            }
        }
        ext2_free_block(fs, inode->i_block[EXT2_IND_BLOCK]);
        inode->i_block[EXT2_IND_BLOCK] = 0;
    }
}

/* ===================== Path traversal ===================== */

static u32 ext2_dir_lookup(ext2_fs_t* fs, u32 dir_ino, const char* name) {
    struct ext2_inode inode;
    if (ext2_read_inode(fs, dir_ino, &inode) != 0) return 0;
    if (!(inode.i_mode & EXT2_S_IFDIR)) return 0;
    u32 size = inode.i_size;
    u32 namelen = str_len(name);
    u8* buf = g_ext2_dir_buf;
    u32 bytes_per_block = fs->block_size;

    for (u32 b = 0; b * bytes_per_block < size; b++) {
        u32 phys = ext2_bmap(fs, &inode, b);
        if (phys == 0) break;
        if (ext2_read_blocks(fs, phys, buf, 1) != 0) break;
        u32 off = 0;
        while (off + sizeof(struct ext2_dir_entry_2) <= bytes_per_block) {
            struct ext2_dir_entry_2* de = (struct ext2_dir_entry_2*)(buf + off);
            if (de->rec_len < 8u || off + de->rec_len > bytes_per_block) break;
            if (de->inode == 0) { off += de->rec_len; continue; }
            /* Ensure the name bytes actually lie within the block before comparing;
             * name_len is attacker-controlled and may run past g_ext2_dir_buf. */
            if (de->name_len == namelen && off + 8u + namelen <= bytes_per_block &&
                memcmp(de->name, name, namelen) == 0) return de->inode;
            off += de->rec_len;
            if (off >= bytes_per_block) break;
        }
    }
    return 0;
}

static void ext2_inode_to_info(ext2_fs_t* fs, const struct ext2_inode* raw, u32 ino, vfs_node_info_t* out) {
    (void)fs;
    if (!out) return;
    memset(out, 0, sizeof(vfs_node_info_t));
    out->used = 1;
    out->id = ino;
    out->size = raw->i_size;
    out->type = (raw->i_mode & EXT2_S_IFDIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
    out->mode = (u16)(raw->i_mode & 0x1FF);
    out->owner_uid = raw->i_uid;
    out->owner_gid = raw->i_gid;
}

static int ext2_dir_list(ext2_fs_t* fs, u32 dir_ino, vfs_node_info_t* out, u32 max_out) {
    struct ext2_inode inode;
    if (ext2_read_inode(fs, dir_ino, &inode) != 0) return -1;
    if (!(inode.i_mode & EXT2_S_IFDIR)) return -1;
    u32 size = inode.i_size;
    u8* buf = g_ext2_list_buf;
    u32 bytes_per_block = fs->block_size;
    u32 count = 0;

    for (u32 b = 0; b * bytes_per_block < size && count < max_out; b++) {
        u32 phys = ext2_bmap(fs, &inode, b);
        if (phys == 0) break;
        if (ext2_read_blocks(fs, phys, buf, 1) != 0) break;
        u32 off = 0;
        while (off + sizeof(struct ext2_dir_entry_2) <= bytes_per_block && count < max_out) {
            struct ext2_dir_entry_2* de = (struct ext2_dir_entry_2*)(buf + off);
            if (de->inode == 0 || de->rec_len == 0) { off += de->rec_len ? de->rec_len : bytes_per_block - off; continue; }
            out[count].used = 1;
            out[count].id = de->inode;
            u32 nl = de->name_len; if (nl >= VFS_MAX_NAME) nl = VFS_MAX_NAME - 1;
            memcpy(out[count].name, de->name, nl);
            out[count].name[nl] = '\0';
            struct ext2_inode ci;
            if (ext2_read_inode(fs, de->inode, &ci) == 0) {
                out[count].size = ci.i_size;
                out[count].type = (ci.i_mode & EXT2_S_IFDIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
            }
            count++;
            off += de->rec_len;
            if (off >= bytes_per_block) break;
        }
    }
    return (int)count;
}

static u32 ext2_path_to_ino(ext2_fs_t* fs, const char* path) {
    if (!path || path[0] != '/') return 0;
    if (str_cmp(path, "/") == 0) return EXT2_ROOT_INO;
    char* name = g_ext2_path_name;
    u32 cur = EXT2_ROOT_INO;
    u32 i = 1;
    while (path[i]) {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;
        u32 ni = 0;
        while (path[i] && path[i] != '/' && ni < EXT2_NAME_LEN) name[ni++] = path[i++];
        name[ni] = '\0';
        if (ni == 0) continue;
        u32 next = ext2_dir_lookup(fs, cur, name);
        if (next == 0) return 0;
        cur = next;
    }
    return cur;
}

/* Resolve to parent inode and leaf name */
static u32 ext2_path_resolve(ext2_fs_t* fs, const char* path, char* leaf, u32 leaf_sz) {
    if (!path || path[0] != '/') return 0;
    leaf[0] = '\0';
    if (str_cmp(path, "/") == 0) return 0;

    char* name = g_ext2_path_name;
    u32 cur = EXT2_ROOT_INO;
    u32 i = 1;

    while (path[i]) {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;
        u32 ni = 0;
        while (path[i] && path[i] != '/' && ni < EXT2_NAME_LEN) name[ni++] = path[i++];
        name[ni] = '\0';

        if (path[i] == '\0') {
            u32 copy_len = ni;
            if (copy_len >= leaf_sz) copy_len = leaf_sz - 1u;
            memcpy(leaf, name, copy_len);
            leaf[copy_len] = '\0';
            return cur;
        }
        /* This component is a parent directory; continue traversal. */
        u32 next = ext2_dir_lookup(fs, cur, name);
        if (next == 0) return 0;
        cur = next;
    }
    return 0;
}

/* ===================== Driver interface ===================== */

int ext2_mount(u32 dev_id, u32 start_lba, void** fs_data) {
    if (!fs_data) return -1;
    if (dev_id != 0) return -1;
    (void)disk_select(0);

    ext2_fs_t* fs = (ext2_fs_t*)adv_alloc(sizeof(ext2_fs_t));
    if (!fs) return -1;
    memset(fs, 0, sizeof(ext2_fs_t));
    fs->dev_id = dev_id;
    fs->start_lba = start_lba;

    u8 sbuf[1024];
    u32 sb_lba = start_lba + 2;
    for (u32 i = 0; i < 2; i++) {
        if (disk_read_sector(sb_lba + i, sbuf + (i * DISK_SECTOR_SIZE)) != 0) { adv_free(fs); return -1; }
    }

    struct ext2_super_block* sb = (struct ext2_super_block*)sbuf;
    if (sb->s_magic != EXT2_SUPER_MAGIC) { adv_free(fs); return -1; }

    fs->inodes_count = sb->s_inodes_count;
    fs->blocks_count = sb->s_blocks_count;
    fs->blocks_per_group = sb->s_blocks_per_group;
    fs->inodes_per_group = sb->s_inodes_per_group;
    fs->first_data_block = sb->s_first_data_block;

    /* Validate attacker-controlled on-disk geometry before deriving sizes.
     * block_size must stay within the fixed 4096-byte scratch buffers, and the
     * divisor fields must be non-zero to avoid #DE on a crafted image. */
    if (sb->s_log_block_size > 2u) { adv_free(fs); return -1; }        /* 1024<<2 == EXT2_MAX_BLOCK_SIZE */
    fs->block_size = 1024u << sb->s_log_block_size;
    fs->block_size_bits = 10 + sb->s_log_block_size;
    if (fs->block_size < EXT2_MIN_BLOCK_SIZE || fs->block_size > EXT2_MAX_BLOCK_SIZE) { adv_free(fs); return -1; }
    fs->inode_size = (sb->s_rev_level == EXT2_DYNAMIC_REV) ? sb->s_inode_size : EXT2_GOOD_OLD_INODE_SIZE;
    if (fs->inode_size < EXT2_GOOD_OLD_INODE_SIZE || fs->inode_size > fs->block_size ||
        (fs->inode_size & (fs->inode_size - 1u)) != 0u) { adv_free(fs); return -1; }
    if (fs->blocks_per_group == 0u || fs->inodes_per_group == 0u || fs->blocks_count == 0u) {
        adv_free(fs); return -1;
    }
    fs->inodes_per_block = fs->block_size / fs->inode_size;

    u32 blocks_per_group = fs->blocks_per_group;
    fs->group_count = (fs->blocks_count + blocks_per_group - 1) / blocks_per_group;
    fs->itb_per_group = (fs->inodes_per_group * fs->inode_size + fs->block_size - 1) / fs->block_size;
    fs->desc_per_block = fs->block_size / sizeof(struct ext2_group_desc);
    fs->gdb_count = (fs->group_count + fs->desc_per_block - 1) / fs->desc_per_block;

    u32 gd_block = (fs->block_size == 1024) ? 2 : 1;
    u32 gd_bytes = fs->group_count * sizeof(struct ext2_group_desc);
    u32 gd_blocks = (gd_bytes + fs->block_size - 1) / fs->block_size;
    if (gd_blocks > 16) gd_blocks = 16;

    u8* gd_buf = g_ext2_mount_gd;
    memset(gd_buf, 0, sizeof(g_ext2_mount_gd));

    for (u32 i = 0; i < gd_blocks; i++) {
        if (ext2_read_blocks(fs, gd_block + i, gd_buf + (i * fs->block_size), 1) != 0) {
            adv_free(fs); return -1;
        }
    }

    fs->group_desc = (struct ext2_group_desc*)gd_buf;
    /* Only gd_blocks worth of descriptors were actually read into the fixed
     * buffer; never advertise more than that (and never more than group_count). */
    {
        u32 loaded = (gd_blocks * fs->block_size) / (u32)sizeof(struct ext2_group_desc);
        fs->gd_count = (loaded < fs->group_count) ? loaded : fs->group_count;
    }

    /* Replay the ext4 journal if present, so metadata is consistent */
    {
        int jrc = ext4_journal_replay(fs);
        if (jrc != 0) {
            serial_writeln("[LastOS] ext4: journal replay failed, mounting anyway");
        } else {
            serial_writeln("[LastOS] ext4: journal replay done");
        }
    }

    /* Detect ext4 incompat features */
    fs->use_extents = 0;
    fs->has_flex_bg = 0;
    if (sb->s_rev_level == EXT2_DYNAMIC_REV) {
        if (sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_EXTENTS) {
            fs->use_extents = 1;
            serial_writeln("[LastOS] ext4: extent tree enabled");
        }
        if (sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_FLEX_BG) {
            fs->has_flex_bg = 1;
        }
    }

    *fs_data = fs;
    return 0;
}

int ext2_umount(void* fs_data) {
    if (!fs_data) return -1;
    ext2_fs_t* fs = (ext2_fs_t*)fs_data;
    if (fs->group_desc && fs->group_desc != (struct ext2_group_desc*)g_ext2_mount_gd) {
        adv_free(fs->group_desc);
    }
    adv_free(fs);
    return 0;
}

int ext2_list(void* fs_data, const char* path, vfs_node_info_t* out, u32 max_out) {
    if (!fs_data || !path || !out) return -1;
    ext2_fs_t* fs = (ext2_fs_t*)fs_data;
    u32 ino = ext2_path_to_ino(fs, path);
    if (ino == 0) return -1;
    return ext2_dir_list(fs, ino, out, max_out);
}

int ext2_read(void* fs_data, const char* path, char* out, u32 out_len) {
    if (!fs_data || !path || !out) return -1;
    ext2_fs_t* fs = (ext2_fs_t*)fs_data;
    u32 ino = ext2_path_to_ino(fs, path);
    if (ino == 0) return -1;
    struct ext2_inode inode;
    if (ext2_read_inode(fs, ino, &inode) != 0) return -1;
    if (inode.i_mode & EXT2_S_IFDIR) return -1;

    u32 size = inode.i_size;
    if (size == 0) return 0;
    u32 to_read = (size < out_len) ? size : out_len;
    u8* buf = g_ext2_read_buf;
    u32 done = 0;
    for (u32 b = 0; done < to_read; b++) {
        u32 phys = ext2_bmap(fs, &inode, b);
        if (phys == 0) break;
        if (ext2_read_blocks(fs, phys, buf, 1) != 0) break;
        u32 chunk = fs->block_size;
        if (done + chunk > to_read) chunk = to_read - done;
        memcpy(out + done, buf, chunk);
        done += chunk;
    }
    if (done < out_len) out[done] = '\0';
    return (int)done;
}

int ext2_write(void* fs_data, const char* path, const char* data, u32 len) {
    if (!fs_data || !path || !data) return -1;
    ext2_fs_t* fs = (ext2_fs_t*)fs_data;
    if (str_cmp(path, "/") == 0) return -1;

    char* leaf = g_ext2_leaf_buf;
    u32 parent = ext2_path_resolve(fs, path, leaf, EXT2_NAME_LEN + 1u);
    if (parent == 0) return -11;
    u32 ino = ext2_dir_lookup(fs, parent, leaf);
    if (ino == 0) return -1;

    struct ext2_inode inode;
    if (ext2_read_inode(fs, ino, &inode) != 0) return -1;
    if (inode.i_mode & EXT2_S_IFDIR) return -1;

    u32 old_size = inode.i_size;
    u32 old_blocks = (old_size + fs->block_size - 1) / fs->block_size;
    u32 new_blocks = (len + fs->block_size - 1) / fs->block_size;
    u8* buf = g_ext2_write_buf;

    for (u32 b = 0; b < new_blocks; b++) {
        u32 phys = ext2_bmap(fs, &inode, b);
        if (phys == 0) {
            phys = ext2_alloc_block(fs, 0);
            if (phys == 0) return -1;
            if (ext2_set_bmap(fs, &inode, b, phys) == 0) return -1;
        }
        memset(buf, 0, sizeof(g_ext2_write_buf));
        u32 offset = b * fs->block_size;
        u32 chunk = len > offset ? len - offset : 0;
        if (chunk > fs->block_size) chunk = fs->block_size;
        if (chunk) memcpy(buf, data + offset, chunk);
        if (ext2_write_blocks(fs, phys, buf, 1) != 0) return -1;
    }

    /* Free extra blocks if shrinking */
    for (u32 b = new_blocks; b < old_blocks; b++) {
        u32 phys = ext2_bmap(fs, &inode, b);
        if (phys) { ext2_free_block(fs, phys); ext2_set_bmap(fs, &inode, b, 0); }
    }

    inode.i_size = len;
    inode.i_blocks = (len + fs->block_size - 1) / fs->block_size * (fs->block_size / 512);
    if (ext2_write_inode(fs, ino, &inode) != 0) return -1;
    return (int)len;
}

int ext2_create(void* fs_data, const char* path, u8 is_dir) {
    if (!fs_data || !path) return -1;
    ext2_fs_t* fs = (ext2_fs_t*)fs_data;
    if (str_cmp(path, "/") == 0) return -1;

    char* leaf = g_ext2_leaf_buf;
    u32 parent = ext2_path_resolve(fs, path, leaf, EXT2_NAME_LEN + 1u);
    if (parent == 0) return -1;
    u32 nlen = str_len(leaf);
    if (nlen == 0) return -12;

    /* Check if already exists */
    if (ext2_dir_lookup(fs, parent, leaf) != 0) return -13;

    /* Allocate inode */
    u32 ino = ext2_alloc_inode(fs);
    if (ino == 0) return -14;

    /* Allocate data block */
    u32 data_block = ext2_alloc_block(fs, 0);
    if (data_block == 0) { ext2_free_inode(fs, ino); return -15; }

    /* Initialize inode */
    struct ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = (is_dir ? EXT2_S_IFDIR : EXT2_S_IFREG) | 0755;
    inode.i_uid = 0;
    inode.i_gid = 0;
    inode.i_links_count = 1;
    inode.i_size = 0;
    inode.i_block[0] = data_block;

    if (is_dir) {
        inode.i_links_count = 2;
        inode.i_size = fs->block_size;

        /* Write . and .. entries */
        u8* dbuf = g_ext2_create_dir_buf;
        memset(dbuf, 0, sizeof(g_ext2_create_dir_buf));

        struct ext2_dir_entry_2 dot_de;
        dot_de.inode = ino;
        dot_de.rec_len = EXT2_DIR_REC_LEN(1);
        dot_de.name_len = 1;
        dot_de.file_type = EXT2_FT_DIR;
        memcpy(dbuf, &dot_de, 8);
        dbuf[8] = '.';

        struct ext2_dir_entry_2 dotdot_de;
        dotdot_de.inode = parent;
        dotdot_de.rec_len = fs->block_size - dot_de.rec_len;
        dotdot_de.name_len = 2;
        dotdot_de.file_type = EXT2_FT_DIR;
        memcpy(dbuf + dot_de.rec_len, &dotdot_de, 8);
        dbuf[dot_de.rec_len + 8] = '.';
        dbuf[dot_de.rec_len + 9] = '.';

        if (ext2_write_blocks(fs, data_block, dbuf, 1) != 0) { ext2_free_inode(fs, ino); ext2_free_block(fs, data_block); return -16; }

        /* Increment parent link count */
        struct ext2_inode pinode;
        if (ext2_read_inode(fs, parent, &pinode) == 0) {
            pinode.i_links_count++;
            ext2_write_inode(fs, parent, &pinode);
        }
    }

    if (ext2_write_inode(fs, ino, &inode) != 0) { ext2_free_inode(fs, ino); ext2_free_block(fs, data_block); return -17; }

    /* Add directory entry */
    u8 ftype = is_dir ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    {
        int add_rc = ext2_dir_add_entry(fs, parent, ino, leaf, ftype);
        if (add_rc != 0) {
            ext2_free_inode(fs, ino); ext2_free_block(fs, data_block);
            return add_rc;
        }
    }
    return 0;
}

int ext2_delete(void* fs_data, const char* path) {
    if (!fs_data || !path) return -1;
    ext2_fs_t* fs = (ext2_fs_t*)fs_data;
    if (str_cmp(path, "/") == 0) return -1;

    char* leaf = g_ext2_leaf_buf;
    u32 parent = ext2_path_resolve(fs, path, leaf, EXT2_NAME_LEN + 1u);
    if (parent == 0) return -1;
    u32 ino = ext2_dir_lookup(fs, parent, leaf);
    if (ino == 0) return -1;
    if (ino == EXT2_ROOT_INO) return -1;

    struct ext2_inode inode;
    if (ext2_read_inode(fs, ino, &inode) != 0) return -1;

    /* Check if directory is empty */
    if (inode.i_mode & EXT2_S_IFDIR) {
        if (inode.i_size > fs->block_size) return -1; /* Not empty (has more than . and ..) */
    }

    if (ext2_dir_remove_entry(fs, parent, leaf) != 0) return -1;

    /* Free blocks */
    ext2_free_inode_blocks(fs, &inode);

    /* Free inode */
    ext2_free_inode(fs, ino);

    /* Decrement parent link count if dir */
    if (inode.i_mode & EXT2_S_IFDIR) {
        struct ext2_inode pinode;
        if (ext2_read_inode(fs, parent, &pinode) == 0) {
            if (pinode.i_links_count > 1) pinode.i_links_count--;
            ext2_write_inode(fs, parent, &pinode);
        }
    }

    /* Clear inode on disk */
    memset(&inode, 0, sizeof(inode));
    ext2_write_inode(fs, ino, &inode);
    return 0;
}

int ext2_info(void* fs_data, const char* path, vfs_node_info_t* out) {
    if (!fs_data || !path || !out) return -1;
    ext2_fs_t* fs = (ext2_fs_t*)fs_data;
    u32 ino = ext2_path_to_ino(fs, path);
    if (ino == 0) return -1;
    struct ext2_inode inode;
    if (ext2_read_inode(fs, ino, &inode) != 0) return -1;
    ext2_inode_to_info(fs, &inode, ino, out);
    return 0;
}

int ext2_format(u32 dev_id, u32 start_lba, u32 total_lba) {
    if (dev_id != 0) return -1;
    (void)disk_select(0);

    u32 block_size = 1024;
    u32 total_blocks = (total_lba * DISK_SECTOR_SIZE) / block_size;
    if (total_blocks < 128) return -1;

    u32 blocks_per_group = 8192;
    u32 inodes_per_group = 256;
    /* The beta formatter provisions one fully described group.  Do not
       advertise a second group until its bitmaps and inode table exist. */
    u32 group_count = 1u;
    u32 inodes_count = group_count * inodes_per_group;
    u32 inode_size = EXT2_GOOD_OLD_INODE_SIZE;
    u32 inodes_per_block = block_size / inode_size;
    u32 itb_per_group = (inodes_per_group + inodes_per_block - 1) / inodes_per_block;
    u32 bgdt_block = 2;
    u32 desc_per_block = block_size / sizeof(struct ext2_group_desc);
    u32 gdb_blocks = (group_count + desc_per_block - 1) / desc_per_block;
    u32 effective_blocks = (total_blocks > blocks_per_group) ? blocks_per_group : total_blocks;
    u32 bbitmap_block = bgdt_block + gdb_blocks;
    u32 ibitmap_block = bbitmap_block + 1;
    u32 itable_block = ibitmap_block + 1;
    u32 first_data_block_num = itable_block + itb_per_group;

    u32 sectors_per_block = block_size / DISK_SECTOR_SIZE;
    /* Formatting initializes metadata; wiping free blocks is a separate erase job. */

    struct ext2_super_block sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_inodes_count = inodes_count;
    sb.s_blocks_count = effective_blocks;
    sb.s_r_blocks_count = 0;
    sb.s_free_blocks_count = effective_blocks - first_data_block_num - 1u;
    sb.s_free_inodes_count = inodes_count - 1;
    sb.s_first_data_block = 0;
    sb.s_log_block_size = 0;
    sb.s_log_frag_size = 0;
    sb.s_blocks_per_group = blocks_per_group;
    sb.s_frags_per_group = blocks_per_group;
    sb.s_inodes_per_group = inodes_per_group;
    sb.s_mtime = 0;
    sb.s_wtime = 0;
    sb.s_mnt_count = 0;
    sb.s_max_mnt_count = 20;
    sb.s_magic = EXT2_SUPER_MAGIC;
    sb.s_state = 1;
    sb.s_errors = 1;
    sb.s_rev_level = EXT2_GOOD_OLD_REV;
    sb.s_def_resuid = 0;
    sb.s_def_resgid = 0;
    sb.s_first_ino = EXT2_GOOD_OLD_FIRST_INO;
    sb.s_inode_size = inode_size;

    u8* sbuf = g_ext2_format_sbuf;
    memset(sbuf, 0, sizeof(g_ext2_format_sbuf));
    memcpy(sbuf, &sb, sizeof(sb));
    u32 sb_lba = start_lba + 2;
    for (u32 i = 0; i < 2; i++) disk_write_sector_fast(sb_lba + i, sbuf + (i * DISK_SECTOR_SIZE));

    struct ext2_group_desc gd;
    memset(&gd, 0, sizeof(gd));
    gd.bg_block_bitmap = bbitmap_block;
    gd.bg_inode_bitmap = ibitmap_block;
    gd.bg_inode_table = itable_block;
    gd.bg_free_blocks_count = (u16)(effective_blocks - first_data_block_num - 1u);
    gd.bg_free_inodes_count = (u16)(inodes_per_group - 1);
    gd.bg_used_dirs_count = 1;

    u8* gdbuf = g_ext2_format_gdbuf;
    memset(gdbuf, 0, sizeof(g_ext2_format_gdbuf));
    memcpy(gdbuf, &gd, sizeof(gd));
    u32 gd_lba = start_lba + (bgdt_block * sectors_per_block);
    for (u32 i = 0; i < sectors_per_block; i++) disk_write_sector_fast(gd_lba + i, gdbuf + (i * DISK_SECTOR_SIZE));

    u8* bbitmap = g_ext2_format_bbitmap;
    memset(bbitmap, 0, sizeof(g_ext2_format_bbitmap));
    for (u32 i = 0; i < first_data_block_num; i++) bbitmap[i / 8] |= (u8)(1u << (i % 8));
    bbitmap[first_data_block_num / 8] |= (u8)(1u << (first_data_block_num % 8));
    u32 bb_lba = start_lba + (bbitmap_block * sectors_per_block);
    for (u32 i = 0; i < sectors_per_block; i++) disk_write_sector_fast(bb_lba + i, bbitmap + (i * DISK_SECTOR_SIZE));

    u8* ibitmap = g_ext2_format_ibitmap;
    memset(ibitmap, 0, sizeof(g_ext2_format_ibitmap));
    ibitmap[0] = 0x07;
    u32 ib_lba = start_lba + (ibitmap_block * sectors_per_block);
    for (u32 i = 0; i < sectors_per_block; i++) disk_write_sector_fast(ib_lba + i, ibitmap + (i * DISK_SECTOR_SIZE));

    struct ext2_inode root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.i_mode = EXT2_S_IFDIR | 0755;
    root_inode.i_uid = 0;
    root_inode.i_size = 1024;
    root_inode.i_gid = 0;
    root_inode.i_links_count = 2;
    root_inode.i_blocks = 2;
    root_inode.i_block[0] = first_data_block_num;

    u8* itable = g_ext2_format_itable;
    memset(itable, 0, sizeof(g_ext2_format_itable));
    /* Inode numbers are one-based: inode 2 lives after inode 1. */
    memcpy(itable + inode_size, &root_inode, sizeof(root_inode));
    u32 it_lba = start_lba + (itable_block * sectors_per_block);
    for (u32 i = 0; i < sectors_per_block; i++) disk_write_sector_fast(it_lba + i, itable + (i * DISK_SECTOR_SIZE));

    struct ext2_dir_entry_2 dot_de;
    dot_de.inode = 2;
    dot_de.rec_len = EXT2_DIR_REC_LEN(1);
    dot_de.name_len = 1;
    dot_de.file_type = EXT2_FT_DIR;
    struct ext2_dir_entry_2 dotdot_de;
    dotdot_de.inode = 2;
    dotdot_de.rec_len = 1024 - dot_de.rec_len;
    dotdot_de.name_len = 2;
    dotdot_de.file_type = EXT2_FT_DIR;
    u8* root_data = g_ext2_format_root_data;
    memset(root_data, 0, sizeof(g_ext2_format_root_data));
    memcpy(root_data, &dot_de, 8);
    root_data[8] = '.';
    memcpy(root_data + dot_de.rec_len, &dotdot_de, 8);
    root_data[dot_de.rec_len + 8] = '.';
    root_data[dot_de.rec_len + 9] = '.';
    u32 data_lba = start_lba + (first_data_block_num * sectors_per_block);
    for (u32 i = 0; i < sectors_per_block; i++) disk_write_sector_fast(data_lba + i, root_data + (i * DISK_SECTOR_SIZE));
    return 0;
}
