# Porting guide: cross/ headers

The host tools read and write PDP-11 a.out objects and archives byte for
byte. The `cross/` directory provides drop-in replacements for the system
headers whose structs describe those on-disk formats, with field widths
pinned to the PDP-11 sizes so an LP64 host lays them out identically.

## Files

- `cross/a.out.h` — `struct exec` (header), `struct ovlhdr`, `struct nlist`
  (symbol), plus the magic numbers and type flags.
- `cross/ar.h` — `struct ar_hdr` (the old binary archive header, ARMAG
  0177545).
- `cross/whoami.h` — empty config header selecting the plain, non-overlay
  build (the original per-site `<whoami.h>` defined `MENLO_OVLY` etc.).

Tools pick these up with `-Icross`.

## The problem

On the PDP-11 `int`=2, `unsigned`=2, `long`=4 bytes. On an LP64 host they
are 4, 4, 8. The original headers declare the on-disk structs with bare
`int`/`unsigned`/`long`, so compiled on the host they are the wrong size
and a raw `fread`/`fwrite` of the struct reads/writes the wrong number of
bytes at the wrong offsets.

PDP-11 and x86-64 are **both little-endian**, so byte order is not an issue
— only the field widths (and, for the archive header, struct padding).

## The fix

Re-declare each on-disk struct with the fixed-width types from
`<stdint.h>`, matching the PDP-11 byte layout. Struct, field, and macro
names are unchanged so the tools compile against these headers untouched.

```c
struct exec {            /* 8 words = 16 bytes */
    int16_t  a_magic;
    uint16_t a_text, a_data, a_bss, a_syms, a_entry, a_unused, a_flag;
};

struct nlist {           /* 12 bytes */
    char     n_name[8];
    char     n_type, n_ovly;
    uint16_t n_value;
};
```

Verified sizes: `exec`=16, `nlist`=12, `ovlhdr`=16.

### Struct packing

`struct ar_hdr` is the one place padding matters:

```c
struct ar_hdr {          /* 26 bytes on the PDP-11 */
    char    ar_name[14];
    int32_t ar_date;     /* offset 14 — only 2-byte aligned on the PDP-11 */
    char    ar_uid, ar_gid;
    int16_t ar_mode;
    int32_t ar_size;     /* offset 22 */
} __attribute__((packed));
```

The PDP-11 only requires 2-byte alignment, so `ar_date` (a 4-byte field)
sits at offset 14. Without `__attribute__((packed))` the host compiler
would insert 2 bytes of padding to 4-byte-align it, making the struct 28+
bytes and shifting every following field. Packing reproduces the 26-byte
PDP-11 layout.

## Verification

Built into the binutils tests. `size` on a real 2.8BSD object matches the
GNU `pdp11-aout-size` oracle exactly, which only works if the `exec` header
is read at the right widths; `nm` decoding a 16-entry symbol table confirms
`nlist` is 12 bytes. See [binutils.md](binutils.md).

## Relationship to the VAX project

The sibling `vax-bsd42-toolchain` uses the same `cross/` technique, but for
ILP32 VAX (`long`→`int32_t`). The PDP-11 gulf is wider (16-bit target), so
the fixed-width treatment is needed more pervasively — not just in `cross/`
but throughout the compiler passes (see [c1.md](c1.md)).
