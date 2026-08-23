//! Path Guard - Memory-Safe Path Canonicalizer in Rust for LastOS
//!
//! Provides strict path validation, '..' resolution, and sanitization
//! without dynamic memory allocation (no_std friendly).

pub const PATH_MAX_LEN: usize = 256;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum PathGuardError {
    EmptyPath,
    PathTooLong,
    InvalidCharacter,
    TraversalEscape,
    NullByteDetected,
}

pub struct PathGuard;

impl PathGuard {
    /// Canonicalize an incoming path buffer safely.
    /// Writes normalized absolute path into `out_buf` and returns the written length.
    pub fn canonicalize(input: &[u8], out_buf: &mut [u8]) -> Result<usize, PathGuardError> {
        if input.is_empty() {
            return Err(PathGuardError::EmptyPath);
        }
        if input.len() >= PATH_MAX_LEN || out_buf.len() < 2 {
            return Err(PathGuardError::PathTooLong);
        }

        // Check for null bytes in input
        for &b in input {
            if b == 0 {
                break; // Stop at null terminator if C-string
            }
            if b == 0x00 {
                return Err(PathGuardError::NullByteDetected);
            }
        }

        // Stack-allocated segment tracker (up to 16 path components)
        let mut segment_starts: [usize; 16] = [0; 16];
        let mut segment_lens: [usize; 16] = [0; 16];
        let mut seg_count = 0;

        let mut idx = 0;
        // Skip leading slashes
        while idx < input.len() && input[idx] == b'/' {
            idx += 1;
        }

        while idx < input.len() && input[idx] != 0 {
            // Find end of current segment
            let start = idx;
            while idx < input.len() && input[idx] != b'/' && input[idx] != 0 {
                let c = input[idx];
                // Reject unsafe control chars or wildcards in path
                if c < 0x20 || c == 0x7F || c == b'*' || c == b'\\' {
                    return Err(PathGuardError::InvalidCharacter);
                }
                idx += 1;
            }
            let len = idx - start;

            if len == 0 || (len == 1 && input[start] == b'.') {
                // Skip "." or empty segments "//"
            } else if len == 2 && input[start] == b'.' && input[start + 1] == b'.' {
                // Handle ".." by popping parent segment if available
                if seg_count > 0 {
                    seg_count -= 1;
                } else {
                    // Prevent escaping root directory
                    return Err(PathGuardError::TraversalEscape);
                }
            } else {
                if seg_count >= 16 {
                    return Err(PathGuardError::PathTooLong);
                }
                segment_starts[seg_count] = start;
                segment_lens[seg_count] = len;
                seg_count += 1;
            }

            while idx < input.len() && input[idx] == b'/' {
                idx += 1;
            }
        }

        // Reconstruct path into out_buf
        let mut out_idx = 0;
        out_buf[out_idx] = b'/';
        out_idx += 1;

        for i in 0..seg_count {
            if i > 0 {
                if out_idx >= out_buf.len() {
                    return Err(PathGuardError::PathTooLong);
                }
                out_buf[out_idx] = b'/';
                out_idx += 1;
            }
            let start = segment_starts[i];
            let len = segment_lens[i];

            if out_idx + len >= out_buf.len() {
                return Err(PathGuardError::PathTooLong);
            }

            out_buf[out_idx..out_idx + len].copy_from_slice(&input[start..start + len]);
            out_idx += len;
        }

        // Null-terminate output buffer
        out_buf[out_idx] = 0;
        Ok(out_idx)
    }
}
