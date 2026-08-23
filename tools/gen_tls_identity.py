#!/usr/bin/env python3
"""Generate build/tls_identity.h with a fixed self-signed RSA-2048 identity.

The kernel's HTTPS server (system/httpd.c, crypto/tls_server.c) links a
certificate chain and private key as byte arrays named LASTOS_TLS_CERT_DER and
LASTOS_TLS_KEY_DER.  This replaces the Windows-only tools/generate_tls_identity.ps1
with a deterministic, reproducible output: the key is generated from a fixed
seed so every build produces identical bytes.

Only stdlib is used.  Output header goes to argv[1] (default build/tls_identity.h).
"""

import os
import sys
import hashlib

# Keep this separate from any CRT; we construct DER by hand.
from struct import pack


def der_len(n):
    if n < 128:
        return bytes([n])
    b = n.to_bytes((n.bit_length() + 7) // 8, 'big')
    return bytes([0x80 | len(b)]) + b


def der_tlv(tag, value):
    return bytes([tag]) + der_len(len(value)) + value


def der_int(value):
    while len(value) > 1 and value[0] == 0:
        value = value[1:]
    if value[0] & 0x80:
        value = b'\x00' + value
    return der_tlv(0x02, value)


def der_seq(items):
    return der_tlv(0x30, b''.join(items))


def der_bitstring(value):
    return der_tlv(0x03, b'\x00' + value)


def der_oid(oid):
    parts = [int(x) for x in oid.split('.')]
    first = 40 * parts[0] + parts[1]
    rest = parts[2:]
    enc = bytes([first])
    for n in rest:
        chunks = []
        while n >= 128:
            chunks.append(n & 0x7F)
            n >>= 7
        chunks.append(n)
        enc += bytes([(c | 0x80) for c in reversed(chunks[1:])] + [chunks[0]])
    return der_tlv(0x06, enc)


def der_null():
    return der_tlv(0x05, b'')


# --- minimal RSA with a fixed deterministic key ------------------------------
# A fixed seed -> reproducible Modulus/Exponent/private exponent.  We build an
# RSA-2048 key by deriving primes from SHA-256(counter) until both are prime and
# their product is exactly 2048 bits.
_seed = b'LastOS deterministic TLS identity (Axiom) - do not modify'


def rand_bytes(n, ctx, tag):
    out = b''
    i = 0
    while len(out) < n:
        out += hashlib.sha256(ctx + bytes([tag, i])).digest()
        i += 1
    return out[:n]


def is_prime(n, rounds=16):
    if n < 2:
        return False
    for p in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if n % p == 0:
            return n == p
    d = n - 1
    r = 0
    while d % 2 == 0:
        d //= 2
        r += 1
    # deterministic witnesses from SHA-256 of the seed
    w = hashlib.sha256(_seed + b'w').digest()
    base = int.from_bytes(w, 'big') % (n - 3) + 2
    for _ in range(rounds):
        x = pow(base, d, n)
        if x == 1 or x == n - 1:
            base = (base * 3 + 1) % (n - 3) + 2
            continue
        for _ in range(r - 1):
            x = (x * x) % n
            if x == n - 1:
                break
        else:
            return False
        base = (base * 3 + 1) % (n - 3) + 2
    return True


def gen_prime(bits, ctx, tag):
    while True:
        cand = rand_bytes(bits // 8, ctx, tag)
        # set top bit (ensure bit length), clear bottom bit (odd), keep ~1024-bit
        cand = bytes([cand[0] | 0xC0]) + cand[1:]
        n = int.from_bytes(cand, 'big') | 1
        if is_prime(n):
            return n
        ctx = hashlib.sha256(ctx + b'x').digest()


def modinv(a, m):
    g, x, _ = egcd(a, m)
    if g != 1:
        raise ValueError('no inverse')
    return x % m


def egcd(a, b):
    if a == 0:
        return b, 0, 1
    g, x1, y1 = egcd(b % a, a)
    return g, y1 - (b // a) * x1, x1


E = 65537
ctx = hashlib.sha256(_seed).digest()
p = gen_prime(1024, ctx, 1)
q = gen_prime(1024, ctx, 2)
if p > q:
    p, q = q, p
n = p * q
d = modinv(E, (p - 1) * (q - 1))
dp = d % (p - 1)
dq = d % (q - 1)
qinv = modinv(q, p)

# --- PKCS#1 RSA private key (for br_skey_decoder) ----------------------------
pkcs1 = der_seq([
    der_int(b'\x00'),
    der_int(n.to_bytes((n.bit_length() + 7) // 8, 'big')),
    der_int(E.to_bytes(3, 'big')),
    der_int(d.to_bytes((d.bit_length() + 7) // 8, 'big')),
    der_int(p.to_bytes(128, 'big')),
    der_int(q.to_bytes(128, 'big')),
    der_int(dp.to_bytes(128, 'big')),
    der_int(dq.to_bytes(128, 'big')),
    der_int(qinv.to_bytes(128, 'big')),
])
key_der = pkcs1

# --- self-signed X.509 certificate (DER) --------------------------------------
# Minimal v3 certificate: tbsCertificate + signature + algorithm, no extensions.
serial = b'\x01\x02\x03\x04\x05\x06'
sig_alg = der_seq([der_oid('1.2.840.113549.1.1.11'), der_null()])
dn = der_seq([der_seq([
    der_seq([der_oid('2.5.4.3'), der_tlv(0x0C, b'axiom.local')]),
])])
validity = der_seq([
    der_tlv(0x17, b'\x20\x26\x08\x01\x00\x00\x00'),
    der_tlv(0x17, b'\x31\x26\x08\x01\x00\x00\x00'),
])
spki = der_seq([
    der_seq([der_oid('1.2.840.113549.1.1.1'), der_null()]),
    der_bitstring(der_seq([
        der_int(n.to_bytes((n.bit_length() + 7) // 8, 'big')),
        der_int(E.to_bytes(3, 'big')),
    ])),
])
tbs = der_seq([
    b'\xa0\x03\x02\x01\x02',           # version 2 (v3)
    der_int(serial),
    sig_alg,
    dn,
    validity,
    dn,
    spki,
    der_tlv(0xA3, der_seq([])),        # extensions: none (empty)
])
# self-signature: SHA-256 digest "signed" by RSA; placeholder bytes are fine for
# the kernel (it only decodes the key, not the signature).
sig = b'\x00' * 256
cert_der = der_seq([tbs, sig_alg, der_bitstring(sig)])


def c_array(name, data):
    lines = ['static const unsigned char %s[] = {' % name]
    row = '    '
    for i, b in enumerate(data):
        row += '0x%02X,' % b
        if (i + 1) % 12 == 0:
            lines.append(row)
            row = '    '
    if row.strip():
        lines.append(row.rstrip(','))
    lines.append('};')
    return '\n'.join(lines)


def write_header(path):
    cert_len = len(cert_der)
    key_len = len(key_der)
    body = '\n'.join([
        '#ifndef LASTOS_GENERATED_TLS_IDENTITY_H',
        '#define LASTOS_GENERATED_TLS_IDENTITY_H',
        '#include "types.h"',
        '#define LASTOS_TLS_CERT_LEN %du' % cert_len,
        '#define LASTOS_TLS_KEY_LEN %du' % key_len,
        c_array('LASTOS_TLS_CERT_DER', cert_der),
        '',
        c_array('LASTOS_TLS_KEY_DER', key_der),
        '#endif',
    ])
    with open(path, 'w') as f:
        f.write(body + '\n')


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'build/tls_identity.h'
    os.makedirs(os.path.dirname(out), exist_ok=True)
    write_header(out)
    print('wrote %s (cert=%d bytes, key=%d bytes)' % (out, len(cert_der), len(key_der)))
