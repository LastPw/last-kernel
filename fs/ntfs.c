#include "fs_ntfs.h"
#include "disk.h"
#include "serial.h"
#include "string.h"
#include "advanced_allocator.h"

/* Static buffers */
static u8 g_ntfs_sector[512];
static u8 g_ntfs_tail[512];       /* Staging for a trailing partial sector */
static u8 g_ntfs_mft_rec[4096];   /* Max MFT record size */
static u8 g_ntfs_index_buf[65536]; /* Max index buffer: 64KB */
static char g_ntfs_path_name[256];
static char g_ntfs_leaf_buf[256];
static char g_ntfs_utf_name[256]; /* Converted filename from UTF-16 */

/* ===================== Low-level I/O ===================== */

/* Read absolute LBA (from partition start_lba + offset) */
static int ntfs_abs_read(u32 dev_id, u32 start_lba, u32 abs_lba, u8* out) {
    (void)dev_id;
    u32 real_lba = start_lba + abs_lba;
    if (disk_read_sector(real_lba, out) != 0) return -1;
    return 0;
}

/* Read a cluster from the filesystem */
static int ntfs_read_cluster(ntfs_fs_t* fs, u64 lcn, u32 cluster_count, u8* buffer) {
    u32 lba = (u32)(lcn * fs->sectors_per_cluster);
    u32 sectors = cluster_count * fs->sectors_per_cluster;
    for (u32 i = 0; i < sectors; i++) {
        if (ntfs_abs_read(fs->dev_id, fs->start_lba, lba + i, buffer + (i * 512)) != 0) return -1;
    }
    return 0;
}

/* ===================== MFT Record Reading ===================== */

static int ntfs_read_mft_record(ntfs_fs_t* fs, u64 mft_ref, u8* out_buf, u32 out_size) {
    u32 rec_num = (u32)(mft_ref & 0xFFFFFFFFu);
    /* Calculate LCN of the MFT record */
    u64 mft_lcn = fs->mft_lcn;
    u64 mft_offset = (u64)rec_num * (u64)fs->bytes_per_mft_record;
    u64 cluster_in_mft = mft_offset / fs->bytes_per_cluster;
    u64 byte_in_cluster = mft_offset % fs->bytes_per_cluster;
    u64 lcn = mft_lcn + cluster_in_mft;

    /* Read the cluster(s) containing this MFT record */
    u32 clusters_needed = (byte_in_cluster + fs->bytes_per_mft_record + fs->bytes_per_cluster - 1) / fs->bytes_per_cluster;
    if (clusters_needed == 0) clusters_needed = 1;

    u8 tmp[8192]; /* Enough for 2 clusters of 4096 */
    if (clusters_needed * fs->bytes_per_cluster > sizeof(tmp)) return -1;
    if (ntfs_read_cluster(fs, lcn, clusters_needed, tmp) != 0) return -1;

    u32 copy_size = fs->bytes_per_mft_record;
    if (copy_size > out_size) copy_size = out_size;
    memcpy(out_buf, tmp + (u32)byte_in_cluster, copy_size);

    /* Verify "FILE" magic */
    ntfs_file_record_t* fr = (ntfs_file_record_t*)out_buf;
    if (fr->magic != NTFS_FILE_RECORD_MAGIC) return -1;

    return 0;
}

/* ===================== Data Run Decoding ===================== */

/*
 * Decode a variable-length integer from a byte stream.
 * Returns the decoded value and advances the pointer.
 * Clamps length to 8 to prevent undefined behavior in shift operations.
 */
static u64 ntfs_decode_vle(const u8** pp, u32 length) {
    u64 val = 0;
    const u8* p = *pp;
    /* Clamp to 8 bytes: a 4-bit field can encode 0..15, but shifting a u64 by
     * more than 63 bits is undefined behavior. NTFS spec limits to 8 anyway. */
    if (length > 8u) length = 8u;
    for (u32 i = 0; i < length; i++) {
        val |= ((u64)p[i]) << (i * 8);
    }
    *pp = p + length;
    return val;
}

/*
 * Parse data runs from a non-resident attribute.
 * Returns the number of runs parsed, or -1 on error.
 */
static int ntfs_parse_runs(const u8* run_data, u32 run_data_len, ntfs_run_t* runs, u32 max_runs) {
    const u8* p = run_data;
    const u8* end = run_data + run_data_len;
    u32 count = 0;
    u64 current_vcn = 0;

    while (p < end && count < max_runs) {
        u8 header = *p++;
        if (header == 0) break; /* End of runs marker */

        u32 len_len = (header & 0x0F);       /* Length field size */
        u32 offset_len = (header >> 4) & 0x0F; /* Offset field size */

        /* Decode run length */
        if (p + len_len > end) return -1;
        const u8* len_p = p;
        runs[count].length = ntfs_decode_vle(&p, len_len);
        runs[count].vcn = current_vcn;
        current_vcn += runs[count].length;

        /* Decode run offset (signed) */
        if (offset_len) {
            if (p + offset_len > end) return -1;
            u64 offset = ntfs_decode_vle(&p, offset_len);
            /* Sign extend if high bit is set */
            /* Note: offset_len is at most 8 (4-bit field from header) */
            if (offset_len < 8 && (offset & ((u64)1 << ((offset_len * 8) - 1)))) {
                offset |= ~((u64)0) << (offset_len * 8);
            }
            /* If offset_len == 8, value is already a full 64-bit signed value */
            s64 signed_offset = (s64)offset;
            if (count > 0) {
                runs[count].lcn = (u64)((s64)runs[count - 1].lcn + signed_offset);
            } else {
                runs[count].lcn = (u64)signed_offset;
            }
        } else {
            runs[count].lcn = 0; /* Sparse run */
        }

        count++;
    }
    return (int)count;
}

/* ===================== Attribute Parsing ===================== */

/* Find an attribute in an MFT record */
static u8* ntfs_find_attr(u8* mft_rec, u32 mft_size, u32 attr_type, u8 index) {
    ntfs_file_record_t* fr = (ntfs_file_record_t*)mft_rec;
    if (fr->magic != NTFS_FILE_RECORD_MAGIC) return 0;

    u32 off = fr->attr_offset;
    u32 found = 0;

    while (off + sizeof(ntfs_attr_header_t) <= mft_size) {
        ntfs_attr_header_t* ah = (ntfs_attr_header_t*)(mft_rec + off);
        if (ah->type == 0xFFFFFFFFu) break; /* End of attributes marker */
        if (ah->length == 0) break;

        if (ah->type == attr_type) {
            if (found == index) return (u8*)ah;
            found++;
        }

        off += ah->length;
        if (off >= mft_size) break;
    }
    return 0;
}

/* ===================== On-disk bounds helpers ===================== */

/*
 * Return a validated pointer to a resident attribute's content.
 * content_offset/content_size are attacker-controlled u16/u32 fields read from
 * the volume; they must be confined both to the attribute and to the containing
 * record buffer. Returns 0 when the span does not fit. *out_size (optional)
 * receives the content size clamped to the attribute.
 */
static u8* ntfs_resident_content(u8* rec, u32 rec_size, u8* attr, u32* out_size) {
    ntfs_attr_header_t* ah;
    ntfs_resident_attr_t* ra;
    u32 attr_off, hdr, co, cs, avail;

    if (!rec || !attr || attr < rec) return 0;
    attr_off = (u32)(attr - rec);
    if (attr_off >= rec_size) return 0;
    avail = rec_size - attr_off;

    hdr = (u32)(sizeof(ntfs_attr_header_t) + sizeof(ntfs_resident_attr_t));
    if (avail < hdr) return 0;

    ah = (ntfs_attr_header_t*)attr;
    if (ah->non_resident) return 0;
    if (ah->length < hdr || ah->length > avail) return 0;

    ra = (ntfs_resident_attr_t*)(attr + sizeof(ntfs_attr_header_t));
    co = ra->content_offset;
    cs = ra->content_size;
    if (co < hdr || co >= ah->length) return 0;
    if (cs > ah->length - co) cs = ah->length - co;   /* clamp to the attribute */

    if (out_size) *out_size = cs;
    return attr + co;
}

/*
 * Resolve the data-run list of a non-resident attribute, with bounds.
 * run_offset is an untrusted u16: ah->length - na->run_offset underflows to a
 * near-4GB length when run_offset exceeds the attribute, which would let the
 * run parser walk far past the record buffer.
 */
static int ntfs_nonresident_run_data(u8* rec, u32 rec_size, u8* attr,
                                    u8** out_run, u32* out_len) {
    ntfs_attr_header_t* ah;
    ntfs_nonresident_attr_t* na;
    u32 attr_off, hdr, ro, avail;

    if (!rec || !attr || attr < rec || !out_run || !out_len) return -1;
    attr_off = (u32)(attr - rec);
    if (attr_off >= rec_size) return -1;
    avail = rec_size - attr_off;

    hdr = (u32)(sizeof(ntfs_attr_header_t) + sizeof(ntfs_nonresident_attr_t));
    if (avail < hdr) return -1;

    ah = (ntfs_attr_header_t*)attr;
    if (!ah->non_resident) return -1;
    if (ah->length < hdr || ah->length > avail) return -1;

    na = (ntfs_nonresident_attr_t*)(attr + sizeof(ntfs_attr_header_t));
    ro = na->run_offset;
    if (ro < hdr || ro >= ah->length) return -1;

    *out_run = attr + ro;
    *out_len = ah->length - ro;
    return 0;
}

/*
 * Resolve the entry span described by an index header.
 * first_entry_offset/total_size come from disk: total_size < first_entry_offset
 * would underflow to a near-4GB span, and an oversized first_entry_offset would
 * push the walk outside the record. Both are rejected here, and the resulting
 * span is clamped to the containing buffer.
 */
static int ntfs_index_span(u8* base, u32 base_size, u8* ih_ptr,
                           u8** out_entries, u32* out_size) {
    ntfs_index_header_t* ih;
    u32 ih_off, feo, ts, entries_off, avail, size;

    if (!base || !ih_ptr || ih_ptr < base || !out_entries || !out_size) return -1;
    ih_off = (u32)(ih_ptr - base);
    if (ih_off > base_size || base_size - ih_off < sizeof(ntfs_index_header_t)) return -1;

    ih = (ntfs_index_header_t*)ih_ptr;
    feo = ih->first_entry_offset;
    ts  = ih->total_size;

    if (feo < sizeof(ntfs_index_header_t)) return -1;
    if (feo > base_size) return -1;                  /* also guards the add below */
    if (ts < feo) return -1;                         /* would underflow */

    entries_off = ih_off + feo;
    if (entries_off > base_size) return -1;
    avail = base_size - entries_off;
    size = ts - feo;
    if (size > avail) size = avail;                  /* clamp to the buffer */

    *out_entries = base + entries_off;
    *out_size = size;
    return 0;
}

/* Read data through data runs into a buffer (for non-resident attributes) */
static int ntfs_read_runs(ntfs_fs_t* fs, ntfs_run_t* runs, u32 run_count,
                          u64 start_vcn, u64 vcn_count, u8* out_buf, u32 buf_size) {
    u64 current_vcn = start_vcn;
    u64 vcn_end = start_vcn + vcn_count;
    u32 bytes_written = 0;
    u32 bytes_per_vcn = fs->bytes_per_cluster;
    u32 max_bytes = buf_size;

    for (u32 ri = 0; ri < run_count && current_vcn < vcn_end; ri++) {
        ntfs_run_t* run = &runs[ri];
        u64 run_start_vcn = run->vcn;
        u64 run_end_vcn = run->vcn + run->length;

        if (run_end_vcn <= current_vcn) continue;

        u64 read_start = (current_vcn > run_start_vcn) ? current_vcn : run_start_vcn;
        u64 read_end = (vcn_end < run_end_vcn) ? vcn_end : run_end_vcn;
        u64 read_vcns = read_end - read_start;

        if (run->lcn == 0) {
            /* Sparse run: skip (zeros already in buffer) */
            u32 sparse = (u32)(read_vcns * bytes_per_vcn);
            if (sparse > max_bytes - bytes_written) sparse = max_bytes - bytes_written;
            bytes_written += sparse;
            current_vcn = read_end;
            continue;
        }

        u64 lcn = run->lcn + (read_start - run_start_vcn);
        u32 sectors = (u32)(read_vcns * fs->sectors_per_cluster);
        u32 lba = (u32)(lcn * fs->sectors_per_cluster);

        for (u32 si = 0; si < sectors && bytes_written < max_bytes; si++) {
            u32 remaining = max_bytes - bytes_written;
            if (remaining >= 512u) {
                if (ntfs_abs_read(fs->dev_id, fs->start_lba, lba + si, out_buf + bytes_written) != 0) return -1;
                bytes_written += 512u;
            } else {
                /* Final partial sector: ntfs_abs_read always writes a full 512
                 * bytes, so stage it and copy only what fits in the caller's
                 * buffer. Writing directly here would overrun out_buf by up to
                 * 511 bytes whenever max_bytes is not a multiple of 512. */
                if (ntfs_abs_read(fs->dev_id, fs->start_lba, lba + si, g_ntfs_tail) != 0) return -1;
                memcpy(out_buf + bytes_written, g_ntfs_tail, remaining);
                bytes_written += remaining;
                break;
            }
        }
        current_vcn = read_end;
    }
    if (bytes_written < max_bytes) {
        memset(out_buf + bytes_written, 0, max_bytes - bytes_written);
    }
    return (int)bytes_written;
}

/* ===================== Filename Conversion ===================== */

/* Convert a UTF-16LE filename to ASCII (lossy but practical) */
static int ntfs_utf16_to_ascii(const u16* utf16, u32 len, char* out, u32 out_sz) {
    u32 j = 0;
    for (u32 i = 0; i < len && j + 1 < out_sz; i++) {
        u16 ch = utf16[i];
        if (ch < 0x80) {
            out[j++] = (char)ch;
        } else if (ch < 0x100) {
            out[j++] = (char)ch; /* Latin-1 range */
        } else {
            out[j++] = '_'; /* Non-ASCII replacement */
        }
    }
    out[j] = '\0';
    return (int)j;
}

/* ===================== File Record Info ===================== */

static void ntfs_fr_get_name(u8* mft_rec, u32 mft_size, char* name, u32 name_sz) {
    u8* attr = ntfs_find_attr(mft_rec, mft_size, NTFS_AT_FILE_NAME, 0);
    if (attr) {
        u32 clen = 0;
        u8* content = ntfs_resident_content(mft_rec, mft_size, attr, &clen);
        if (content && clen >= sizeof(ntfs_filename_attr_t)) {
            ntfs_filename_attr_t* fn = (ntfs_filename_attr_t*)content;
            u32 avail_chars = (clen - (u32)sizeof(ntfs_filename_attr_t)) / 2u;
            u32 nlen = fn->name_length;
            if (nlen > avail_chars) nlen = avail_chars;  /* name_length is untrusted */
            ntfs_utf16_to_ascii(fn->name, nlen, name, name_sz);
            return;
        }
    }
    str_ncpy(name, "unknown", name_sz);
}

static u64 ntfs_fr_get_parent_mft(u8* mft_rec, u32 mft_size) {
    u8* attr = ntfs_find_attr(mft_rec, mft_size, NTFS_AT_FILE_NAME, 0);
    if (attr) {
        u32 clen = 0;
        u8* content = ntfs_resident_content(mft_rec, mft_size, attr, &clen);
        if (content && clen >= sizeof(ntfs_filename_attr_t)) {
            ntfs_filename_attr_t* fn = (ntfs_filename_attr_t*)content;
            return fn->parent_mft_ref;
        }
    }
    return 0;
}

static u64 ntfs_fr_get_size(u8* mft_rec, u32 mft_size) {
    u8* attr = ntfs_find_attr(mft_rec, mft_size, NTFS_AT_DATA, 0);
    if (attr) {
        ntfs_attr_header_t* ah = (ntfs_attr_header_t*)attr;
        if (ah->non_resident) {
            ntfs_nonresident_attr_t* na = (ntfs_nonresident_attr_t*)(attr + sizeof(ntfs_attr_header_t));
            return na->real_size;
        } else {
            ntfs_resident_attr_t* ra = (ntfs_resident_attr_t*)(attr + sizeof(ntfs_attr_header_t));
            return ra->content_size;
        }
    }
    return 0;
}

static int ntfs_is_directory(u8* mft_rec, u32 mft_size) {
    ntfs_file_record_t* fr = (ntfs_file_record_t*)mft_rec;
    return (fr->flags & NTFS_FR_FLAG_DIRECTORY) ? 1 : 0;
}

/* ===================== Index Entry Parsing ===================== */

/* Walk index entries and fill vfs_node_info_t array */
static int ntfs_walk_index_entries(ntfs_fs_t* fs, u8* index_start, u32 index_size,
                                    vfs_node_info_t* out, u32 max_out) {
    u32 count = 0;
    u32 off = 0;

    while (off + sizeof(ntfs_index_entry_t) <= index_size && count < max_out) {
        ntfs_index_entry_t* entry = (ntfs_index_entry_t*)(index_start + off);
        if (entry->length == 0 || entry->length < sizeof(ntfs_index_entry_t)) break;

        /* Skip the last entry and empty entries */
        if (entry->flags & NTFS_IDX_FLAG_LAST) break;

        u64 mft_ref = entry->mft_ref;
        if (mft_ref == 0) { off += entry->length; continue; }

        /* Read the file record to get file info */
        u8* fr_buf = g_ntfs_mft_rec;
        if (ntfs_read_mft_record(fs, mft_ref, fr_buf, sizeof(g_ntfs_mft_rec)) != 0) {
            off += entry->length;
            continue;
        }

        /* Get filename from FILE_NAME attribute in the child record */
        char child_name[256];
        ntfs_fr_get_name(fr_buf, fs->bytes_per_mft_record, child_name, sizeof(child_name));

        /* Skip . and .. entries */
        if (str_cmp(child_name, ".") == 0 || str_cmp(child_name, "..") == 0) {
            off += entry->length;
            continue;
        }

        out[count].used = 1;
        out[count].id = (u32)(mft_ref & 0xFFFFFFFFu);
        str_ncpy(out[count].name, child_name, VFS_MAX_NAME - 1);
        out[count].name[VFS_MAX_NAME - 1] = '\0';
        out[count].size = (u32)ntfs_fr_get_size(fr_buf, fs->bytes_per_mft_record);
        out[count].type = ntfs_is_directory(fr_buf, fs->bytes_per_mft_record) ? VFS_NODE_DIR : VFS_NODE_FILE;
        count++;

        off += entry->length;
    }
    return (int)count;
}

/* List entries in a directory by parsing INDEX_ROOT and INDEX_ALLOCATION */
static int ntfs_list_dir(ntfs_fs_t* fs, u8* mft_rec, vfs_node_info_t* out, u32 max_out) {
    u32 total = 0;

    /* First, try INDEX_ROOT (resident attribute for small directories) */
    u8* idx_root_attr = ntfs_find_attr(mft_rec, fs->bytes_per_mft_record, NTFS_AT_INDEX_ROOT, 0);
    if (idx_root_attr) {
        u32 clen = 0;
        u8* content = ntfs_resident_content(mft_rec, fs->bytes_per_mft_record, idx_root_attr, &clen);
        if (content && clen > sizeof(ntfs_index_root_header_t)) {
            u8* entries = 0;
            u32 entries_size = 0;
            u8* ih_ptr = content + sizeof(ntfs_index_root_header_t);
            if (ntfs_index_span(content, clen, ih_ptr, &entries, &entries_size) == 0) {
                int cnt = ntfs_walk_index_entries(fs, entries, entries_size, out, max_out);
                if (cnt > 0) total += (u32)cnt;
            }

            /* Check if we need to also read INDEX_ALLOCATION */
            /* If flags has 0x01, there are index allocation blocks */
        }
    }

    /* If there's an INDEX_ALLOCATION attribute, read index blocks too */
    u8* idx_alloc_attr = ntfs_find_attr(mft_rec, fs->bytes_per_mft_record, NTFS_AT_INDEX_ALLOCATION, 0);
    if (idx_alloc_attr && total < max_out) {
        ntfs_attr_header_t* ah = (ntfs_attr_header_t*)idx_alloc_attr;
        if (ah->non_resident) {
            ntfs_nonresident_attr_t* na = (ntfs_nonresident_attr_t*)(idx_alloc_attr + sizeof(ntfs_attr_header_t));
            u8* run_data = 0;
            u32 run_data_len = 0;
            if (ntfs_nonresident_run_data(mft_rec, fs->bytes_per_mft_record, idx_alloc_attr,
                                          &run_data, &run_data_len) != 0) return (int)total;

            ntfs_run_t runs[NTFS_MAX_RUNS];
            int run_count = ntfs_parse_runs(run_data, run_data_len, runs, NTFS_MAX_RUNS);
            if (run_count > 0) {
                u64 total_vcns = na->last_vcn - na->start_vcn + 1;
                u8* idx_buf = g_ntfs_index_buf;
                u32 idx_buf_size = sizeof(g_ntfs_index_buf);

                /* Read all index allocation blocks */
                ntfs_read_runs(fs, runs, (u32)run_count, na->start_vcn, total_vcns, idx_buf, idx_buf_size);

                /* Parse each index block */
                u32 bytes_per_block = fs->bytes_per_index_record;
                if (bytes_per_block < sizeof(ntfs_index_block_header_t) + sizeof(ntfs_index_header_t)) {
                    return (int)total;   /* bogus geometry: also guards the divide below */
                }
                u32 block_count = idx_buf_size / bytes_per_block;
                if (block_count > 64) block_count = 64;

                for (u32 bi = 0; bi < block_count && total < max_out; bi++) {
                    u8* block = idx_buf + (bi * bytes_per_block);
                    ntfs_index_block_header_t* ibh = (ntfs_index_block_header_t*)block;
                    if (ibh->magic != NTFS_INDEX_RECORD_MAGIC) continue;

                    u8* entries = 0;
                    u32 entries_size = 0;
                    if (ntfs_index_span(block, bytes_per_block, block + 0x18,
                                        &entries, &entries_size) != 0) continue;

                    int cnt = ntfs_walk_index_entries(fs, entries, entries_size, out + total, max_out - total);
                    if (cnt > 0) total += (u32)cnt;
                }
            }
        }
    }

    return (int)total;
}

/* ===================== Path Traversal ===================== */

/* Find the MFT reference of a child file/directory */
static u64 ntfs_find_child(ntfs_fs_t* fs, u64 dir_mft_ref, const char* name) {
    u8* fr_buf = g_ntfs_mft_rec;
    u8* tmp_buf = g_ntfs_index_buf; /* Reuse index buffer for child records */

    if (ntfs_read_mft_record(fs, dir_mft_ref, fr_buf, fs->bytes_per_mft_record) != 0) return 0;

    /* First check INDEX_ROOT */
    u8* idx_root_attr = ntfs_find_attr(fr_buf, fs->bytes_per_mft_record, NTFS_AT_INDEX_ROOT, 0);
    if (idx_root_attr) {
        u32 clen = 0;
        u8* content = ntfs_resident_content(fr_buf, fs->bytes_per_mft_record, idx_root_attr, &clen);
        if (content && clen > sizeof(ntfs_index_root_header_t)) {
            u8* entries = 0;
            u32 entries_size = 0;
            if (ntfs_index_span(content, clen, content + sizeof(ntfs_index_root_header_t),
                                &entries, &entries_size) == 0) {
                u32 off = 0;

                while (off + sizeof(ntfs_index_entry_t) <= entries_size) {
                    ntfs_index_entry_t* entry = (ntfs_index_entry_t*)(entries + off);
                    if (entry->length == 0 || entry->length < sizeof(ntfs_index_entry_t)) break;
                    if (entry->flags & NTFS_IDX_FLAG_LAST) break;

                    if (entry->mft_ref != 0) {
                        /* Read child to get its name */
                        if (ntfs_read_mft_record(fs, entry->mft_ref, tmp_buf, fs->bytes_per_mft_record) == 0) {
                            char child_name[256];
                            ntfs_fr_get_name(tmp_buf, fs->bytes_per_mft_record, child_name, sizeof(child_name));
                            if (str_cmp(child_name, name) == 0) {
                                return entry->mft_ref;
                            }
                        }
                    }
                    off += entry->length;
                }
            }
        }
    }

    /* Check INDEX_ALLOCATION */
    u8* idx_alloc_attr = ntfs_find_attr(fr_buf, fs->bytes_per_mft_record, NTFS_AT_INDEX_ALLOCATION, 0);
    if (idx_alloc_attr) {
        ntfs_attr_header_t* ah = (ntfs_attr_header_t*)idx_alloc_attr;
        if (ah->non_resident) {
            ntfs_nonresident_attr_t* na = (ntfs_nonresident_attr_t*)(idx_alloc_attr + sizeof(ntfs_attr_header_t));
            u8* run_data = 0;
            u32 run_data_len = 0;
            if (ntfs_nonresident_run_data(fr_buf, fs->bytes_per_mft_record, idx_alloc_attr,
                                          &run_data, &run_data_len) != 0) return 0;

            ntfs_run_t runs[NTFS_MAX_RUNS];
            int run_count = ntfs_parse_runs(run_data, run_data_len, runs, NTFS_MAX_RUNS);
            if (run_count > 0) {
                u64 total_vcns = na->last_vcn - na->start_vcn + 1;
                u8* idx_buf = g_ntfs_index_buf;
                u32 idx_buf_size = sizeof(g_ntfs_index_buf);

                ntfs_read_runs(fs, runs, (u32)run_count, na->start_vcn, total_vcns, idx_buf, idx_buf_size);

                u32 bytes_per_block = fs->bytes_per_index_record;
                if (bytes_per_block < sizeof(ntfs_index_block_header_t) + sizeof(ntfs_index_header_t)) {
                    return 0;   /* bogus geometry: also guards the divide below */
                }
                u32 block_count = idx_buf_size / bytes_per_block;
                if (block_count > 64) block_count = 64;

                for (u32 bi = 0; bi < block_count; bi++) {
                    u8* block = idx_buf + (bi * bytes_per_block);
                    ntfs_index_block_header_t* ibh = (ntfs_index_block_header_t*)block;
                    if (ibh->magic != NTFS_INDEX_RECORD_MAGIC) continue;

                    u8* entries = 0;
                    u32 entries_size = 0;
                    if (ntfs_index_span(block, bytes_per_block, block + 0x18,
                                        &entries, &entries_size) != 0) continue;
                    u32 off = 0;

                    while (off + sizeof(ntfs_index_entry_t) <= entries_size) {
                        ntfs_index_entry_t* entry = (ntfs_index_entry_t*)(entries + off);
                        if (entry->length == 0 || entry->length < sizeof(ntfs_index_entry_t)) break;
                        if (entry->flags & NTFS_IDX_FLAG_LAST) break;

                        if (entry->mft_ref != 0) {
                            if (ntfs_read_mft_record(fs, entry->mft_ref, tmp_buf, fs->bytes_per_mft_record) == 0) {
                                char child_name[256];
                                ntfs_fr_get_name(tmp_buf, fs->bytes_per_mft_record, child_name, sizeof(child_name));
                                if (str_cmp(child_name, name) == 0) {
                                    return entry->mft_ref;
                                }
                            }
                        }
                        off += entry->length;
                    }
                }
            }
        }
    }

    return 0;
}

/* Resolve a path to an MFT reference */
static u64 ntfs_path_to_mft(ntfs_fs_t* fs, const char* path) {
    if (!path || path[0] != '/') return 0;
    if (str_cmp(path, "/") == 0) return NTFS_FILE_ROOT;

    char* name = g_ntfs_path_name;
    u64 cur = NTFS_FILE_ROOT;
    u32 i = 1;

    while (path[i]) {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;

        u32 ni = 0;
        while (path[i] && path[i] != '/' && ni < 255) name[ni++] = path[i++];
        name[ni] = '\0';
        if (ni == 0) continue;

        u64 next = ntfs_find_child(fs, cur, name);
        if (next == 0) return 0;
        cur = next;
    }
    return cur;
}

/* Resolve to parent MFT and leaf name */
static u64 ntfs_path_resolve(ntfs_fs_t* fs, const char* path, char* leaf, u32 leaf_sz) {
    if (!path || path[0] != '/') return 0;
    if (str_cmp(path, "/") == 0) return 0;
    leaf[0] = '\0';

    char* name = g_ntfs_path_name;
    u64 cur = NTFS_FILE_ROOT;
    u32 i = 1;

    while (path[i]) {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;

        u32 ni = 0;
        while (path[i] && path[i] != '/' && ni < 255) name[ni++] = path[i++];
        name[ni] = '\0';

        if (path[i] == '\0') {
            u32 copy_len = ni;
            if (copy_len >= leaf_sz) copy_len = leaf_sz - 1;
            memcpy(leaf, name, copy_len);
            leaf[copy_len] = '\0';
            return cur;
        }

        u64 next = ntfs_find_child(fs, cur, name);
        if (next == 0) return 0;
        cur = next;
    }
    return 0;
}

/* ===================== File Reading ===================== */

static int ntfs_read_file_data(ntfs_fs_t* fs, u64 mft_ref, char* out, u32 out_len) {
    u8* fr_buf = g_ntfs_mft_rec;
    if (ntfs_read_mft_record(fs, mft_ref, fr_buf, fs->bytes_per_mft_record) != 0) return -1;

    u8* data_attr = ntfs_find_attr(fr_buf, fs->bytes_per_mft_record, NTFS_AT_DATA, 0);
    if (!data_attr) return -1;

    ntfs_attr_header_t* ah = (ntfs_attr_header_t*)data_attr;

    if (!ah->non_resident) {
        /* Resident data: direct copy, bounded by the validated content span */
        u32 clen = 0;
        u8* content = ntfs_resident_content(fr_buf, fs->bytes_per_mft_record, data_attr, &clen);
        if (!content) return -1;
        u32 data_size = clen;
        if (data_size > out_len) data_size = out_len;
        memcpy(out, content, data_size);
        if (data_size < out_len) out[data_size] = '\0';
        return (int)data_size;
    }

    /* Non-resident: read via data runs */
    ntfs_nonresident_attr_t* na = (ntfs_nonresident_attr_t*)(data_attr + sizeof(ntfs_attr_header_t));
    u64 file_size = na->real_size;
    if (file_size > out_len) file_size = out_len;
    if (file_size == 0) return 0;

    u8* run_data = 0;
    u32 run_data_len = 0;
    if (ntfs_nonresident_run_data(fr_buf, fs->bytes_per_mft_record, data_attr,
                                  &run_data, &run_data_len) != 0) return -1;

    ntfs_run_t runs[NTFS_MAX_RUNS];
    int run_count = ntfs_parse_runs(run_data, run_data_len, runs, NTFS_MAX_RUNS);
    if (run_count <= 0) return -1;

    if (fs->bytes_per_cluster == 0u) return -1;
    u64 total_vcns = (file_size + fs->bytes_per_cluster - 1) / fs->bytes_per_cluster;
    return ntfs_read_runs(fs, runs, (u32)run_count, 0, total_vcns, (u8*)out, (u32)file_size);
}

/* ===================== Driver Interface ===================== */

int ntfs_mount(u32 dev_id, u32 start_lba, void** fs_data) {
    if (!fs_data) return -1;
    (void)disk_select(0);

    ntfs_fs_t* fs = (ntfs_fs_t*)adv_alloc(sizeof(ntfs_fs_t));
    if (!fs) return -1;
    memset(fs, 0, sizeof(ntfs_fs_t));
    fs->dev_id = dev_id;
    fs->start_lba = start_lba;

    /* Read boot sector */
    if (ntfs_abs_read(dev_id, start_lba, 0, g_ntfs_sector) != 0) {
        adv_free(fs);
        return -1;
    }

    ntfs_boot_sector_t* bs = (ntfs_boot_sector_t*)g_ntfs_sector;

    /* Verify NTFS signature */
    if (memcmp(bs->oem_id, NTFS_OEM_ID, 8) != 0) {
        adv_free(fs);
        return -1;
    }

    fs->bytes_per_sector = bs->bytes_per_sector;
    fs->sectors_per_cluster = bs->sectors_per_cluster;
    fs->bytes_per_cluster = fs->bytes_per_sector * fs->sectors_per_cluster;
    fs->mft_lcn = bs->mft_lcn;
    fs->total_sectors = (u32)bs->total_sectors;

    /* Validate geometry BEFORE it is used as a divisor below. A boot sector with
     * sectors_per_cluster == 0 (or a non-512 sector size) would otherwise fault
     * the kernel with #DE while decoding the record sizes. */
    if (fs->bytes_per_sector != 512u || fs->sectors_per_cluster == 0u ||
        fs->bytes_per_cluster == 0u || fs->bytes_per_cluster > 65536u) {
        adv_free(fs);
        return -1;
    }

    /* Decode clusters_per_mft_record */
    s8 cpmr = (s8)bs->clusters_per_mft_record;
    if (cpmr < 0) {
        fs->bytes_per_mft_record = (u32)1 << (-cpmr);
        fs->clusters_per_mft_record = (fs->bytes_per_mft_record + fs->bytes_per_cluster - 1) / fs->bytes_per_cluster;
    } else {
        fs->clusters_per_mft_record = (u32)cpmr;
        fs->bytes_per_mft_record = fs->clusters_per_mft_record * fs->bytes_per_cluster;
    }
    fs->mft_record_size = fs->bytes_per_mft_record;

    /* Decode clusters_per_index_buffer */
    s8 cpib = (s8)bs->clusters_per_index_buffer;
    if (cpib < 0) {
        fs->bytes_per_index_record = (u32)1 << (-cpib);
        fs->clusters_per_index_record = (fs->bytes_per_index_record + fs->bytes_per_cluster - 1) / fs->bytes_per_cluster;
    } else {
        fs->clusters_per_index_record = (u32)cpib;
        fs->bytes_per_index_record = fs->clusters_per_index_record * fs->bytes_per_cluster;
    }
    fs->index_record_size = fs->bytes_per_index_record;

    /* Validate key values. Record sizes must be non-zero (they are used as
     * divisors) and must fit the fixed g_ntfs_mft_rec / g_ntfs_index_buf staging
     * buffers, which is what bounds every parser downstream. */
    if (fs->bytes_per_mft_record == 0u || fs->bytes_per_mft_record > sizeof(g_ntfs_mft_rec) ||
        fs->bytes_per_index_record == 0u || fs->bytes_per_index_record > sizeof(g_ntfs_index_buf)) {
        adv_free(fs);
        return -1;
    }

    serial_write("[LastOS] NTFS: mounted, mft_lcn=");
    serial_write_dec((u32)fs->mft_lcn);
    serial_write(" mft_rec=");
    serial_write_dec(fs->bytes_per_mft_record);
    serial_write(" idx_rec=");
    serial_write_dec(fs->bytes_per_index_record);
    serial_writeln("");

    *fs_data = fs;
    return 0;
}

int ntfs_umount(void* fs_data) {
    if (!fs_data) return -1;
    adv_free(fs_data);
    return 0;
}

int ntfs_list(void* fs_data, const char* path, vfs_node_info_t* out, u32 max_out) {
    if (!fs_data || !path || !out) return -1;
    ntfs_fs_t* fs = (ntfs_fs_t*)fs_data;

    u64 mft_ref = ntfs_path_to_mft(fs, path);
    if (mft_ref == 0) return -1;

    u8* fr_buf = g_ntfs_mft_rec;
    if (ntfs_read_mft_record(fs, mft_ref, fr_buf, fs->bytes_per_mft_record) != 0) return -1;

    if (!ntfs_is_directory(fr_buf, fs->bytes_per_mft_record)) return -1;

    return ntfs_list_dir(fs, fr_buf, out, max_out);
}

int ntfs_read(void* fs_data, const char* path, char* out, u32 out_len) {
    if (!fs_data || !path || !out) return -1;
    ntfs_fs_t* fs = (ntfs_fs_t*)fs_data;

    u64 mft_ref = ntfs_path_to_mft(fs, path);
    if (mft_ref == 0) return -1;

    u8* fr_buf = g_ntfs_mft_rec;
    if (ntfs_read_mft_record(fs, mft_ref, fr_buf, fs->bytes_per_mft_record) != 0) return -1;

    if (ntfs_is_directory(fr_buf, fs->bytes_per_mft_record)) return -1;

    return ntfs_read_file_data(fs, mft_ref, out, out_len);
}

int ntfs_info(void* fs_data, const char* path, vfs_node_info_t* out) {
    if (!fs_data || !path || !out) return -1;
    ntfs_fs_t* fs = (ntfs_fs_t*)fs_data;

    u64 mft_ref = ntfs_path_to_mft(fs, path);
    if (mft_ref == 0) return -1;

    u8* fr_buf = g_ntfs_mft_rec;
    if (ntfs_read_mft_record(fs, mft_ref, fr_buf, fs->bytes_per_mft_record) != 0) return -1;

    memset(out, 0, sizeof(vfs_node_info_t));
    out->used = 1;
    out->id = (u32)(mft_ref & 0xFFFFFFFFu);
    out->size = (u32)ntfs_fr_get_size(fr_buf, fs->bytes_per_mft_record);
    out->type = ntfs_is_directory(fr_buf, fs->bytes_per_mft_record) ? VFS_NODE_DIR : VFS_NODE_FILE;
    return 0;
}

/* Write/create/delete/format are NOT supported in this read-only driver */
int ntfs_write(void* fs_data, const char* path, const char* data, u32 len) {
    (void)fs_data; (void)path; (void)data; (void)len;
    return -1;
}

int ntfs_create(void* fs_data, const char* path, u8 is_dir) {
    (void)fs_data; (void)path; (void)is_dir;
    return -1;
}

int ntfs_delete(void* fs_data, const char* path) {
    (void)fs_data; (void)path;
    return -1;
}

int ntfs_format(u32 dev_id, u32 start_lba, u32 total_lba) {
    (void)dev_id; (void)start_lba; (void)total_lba;
    return -1;
}
