# The trophy files the payload reads

Everything here was worked out by looking at files from a PS5 on firmware 12.20. Nothing is
decrypted: both files are stored in the clear. All numbers are big-endian.

## Where they are

| Path | What |
|---|---|
| `/user/home/<user id>/trophy2/nobackup/data/<set>/TRPTITLE.DAT` | One user's progress in one trophy set |
| `/user/trophy2/nobackup/conf/<set>/TROPHY.UCP` | The set itself: names, grades, pictures. Shared by all users |

`<user id>` is the local user's ID as 8 hex digits (for example `118e876e`).
`<set>` is the trophy set ID, such as `NPWR12345_00`.

Trophies of PS4 games are kept elsewhere (`/user/home/<user id>/trophy/data/sce_trop/trophy.img`,
an encrypted image) and are not read by this payload.

## TRPTITLE.DAT ("T2PD")

A 0x40-byte header, then fixed-size blocks.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | `T2PD` |
| 0x08 | 8 | Block size (0x420) |
| 0x20 | 8 | Number of blocks |

Each block is 0x400 bytes of data followed by a 0x20-byte hash of that data. Take the data
parts of all blocks, in order, and join them: every offset below is into that joined stream.

The stream starts with a table of contents:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | `T2TD` |
| 0x08 | 4 | Number of entries |
| 0x30 | 0x20 each | Entries |

An entry:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | Record type |
| 0x04 | 4 | Size of one record's body |
| 0x0c | 4 | Number of records |
| 0x10 | 8 | Offset of the first record |

Every record is a 0x10-byte header that repeats the type and body size, then the body. Records
of one type sit one after another.

| Type | Body | What |
|---|---|---|
| 0x300 | 0x480 | Starts with the trophy set ID |
| 0x500 | 0xc0 | One per trophy: ID, grade (1 platinum, 2 gold, 3 silver, 4 bronze) |
| 0x800 | 0x50 | One per trophy: **earned or not, and when** |

The payload only needs type 0x800:

| Offset in body | Size | Field |
|---|---|---|
| 0x00 | 4 | Trophy ID |
| 0x04 | 4 | Flags. Bit 0 set = earned |
| 0x10 | 8 | When it was earned: microseconds since 0001-01-01 00:00 UTC |

To get Unix time, subtract 62,135,596,800,000,000 and divide by 1,000,000.

The other record types (0x1, 0x2, 0x100, 0x400, 0x600, 0x700) hold dates, the user ID, hashes
and counters. The payload skips them.

## TROPHY.UCP

A plain archive, no compression.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | Magic `b2 28 c6 0a` |
| 0x04 | 4 | Version (1) |
| 0x08 | 8 | Size of the whole file |
| 0x10 | 4 | Number of files |
| 0x14 | 4 | Size of one table row (0x40) |
| 0x60 | | The table |

A table row:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 32 | File name, zero padded |
| 0x20 | 8 | Offset of the file |
| 0x28 | 8 | Size of the file |

Files found inside:

- `tropconf.json`: the trophy list. `trophyNpCommId`, `trophyDefinitionRevision`, `platform`,
  `defaultLanguage`, and `trophies`: `[{ "id": "0001", "grade": "G", "hidden": true, ... }]`.
- `tropmeta_<language>.json`, one per language (`en-US`, `pt-BR`, ...): the game's name in
  `metadata.titleMetadata.name` and each trophy's `name` and `detail` in `metadata.trophyMetadata`.
- `icon0_<language>.png`: the game's picture, 512 x 512. The payload sends it once per game,
  shrunk to a 192-pixel JPEG.
- `tropNNNN.png`: one 512 x 512 picture per trophy. The payload does not send these.

The payload sends `tropconf.json` and one `tropmeta` file to the server exactly as they are, the
first time the server meets a trophy set. It prefers `en-US`, then `en-GB`, then the set's
`defaultLanguage`.

## Checking the layout yourself

`tests/make_fixture.py` writes both formats from scratch, and `tests/unit.c` reads them back
with the payload's own code. `make test` runs both.
