# ps5-trophy-share

A PS5 payload that puts your trophies on a public page: per game, by rarity and by date.

![Bronze, silver, gold and platinum glyphs](assets/trophies.svg)

It reads the trophy files of the user who is signed in on the console and sends them to
[Trophy Share](https://trophyshare.pages.dev), where you get a page with a nickname, a picture and
a player ID to hand out. It then stays in the background and sends new trophies a moment after
you earn them.

Not affiliated with Sony Interactive Entertainment.

## Use it

You need a PS5 that can run payloads (an ELF loader on port 9021) and an internet connection on it.

1. Open <https://trophyshare.pages.dev>, sign in with your e-mail and pick a nickname.
2. Download `ps5-trophy-share.elf` from the [latest release](../../releases/latest) and send it to
   the console:

   ```
   nc 192.168.0.10 9021 < ps5-trophy-share.elf
   ```

3. The console shows a message with a code such as `7K3M-9QXZ`. Type it at
   <https://trophyshare.pages.dev/link/>. The code lasts 10 minutes.
4. Done. The console says how many trophies went to your page.

After the console restarts, send the payload again. It remembers the link, so there is no new code.

| | |
|---|---|
| Another user on the same console | Switch user and send the payload again. Each user links to their own page. |
| Unlink | Remove the console on your page. The payload then offers a new code. |
| Stop the payload | Send `ps5-trophy-share-stop.elf`. |
| No way to type a code | On your page choose "Make a key by hand" and save the key on the console as `/data/trophyshare/key` (over FTP). |
| Something is wrong | Read `/data/trophyshare/log.txt` on the console. |

## What it reads and sends

- It reads `TRPTITLE.DAT` (your progress) and `TROPHY.UCP` (the trophy list) for each PS5 game.
  [docs/FORMATS.md](docs/FORMATS.md) describes both files.
- It sends trophy set IDs, which trophies you earned and when, and the console user's name as the
  label of the link. The first time the server meets a game it also gets that game's trophy names.
  [docs/PROTOCOL.md](docs/PROTOCOL.md) lists every call.
- It sends nothing else: no account IDs, no serial numbers, no save data.
- Trophies of PS4 games played on the PS5 are stored in another, encrypted format and are not read.
- The server trusts what a console tells it. Trophy Share is for showing your trophies to
  friends; it does not try to detect edited trophy files.

## Build it

```
git clone --recursive https://github.com/heydemoura/ps5-trophy-share
cd ps5-trophy-share
make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
```

You need [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk). The result is
`ps5-trophy-share.elf` and `ps5-trophy-share-stop.elf`.

To send trophies to your own server instead, build with `make API_URL=https://your.server`, or
write the address in `/data/trophyshare/api` on the console.

## Test it without a console

```
make test
```

This builds the same program for your PC, makes a folder that looks like a console's disk with
made-up trophies (`tests/make_fixture.py`), and runs the program against a stand-in server
(`tests/e2e.py`): linking with a code, the first sync, later syncs, a removed console, a key made
by hand and an unreachable server. `tests/unit.c` checks the file readers, also against damaged files.

Checked on a real PS5 (firmware 12.20): HTTPS from the console, linking with a code, reading a real
trophy set and sending its trophy list, staying in the background, a second copy replacing the
first, and the stop payload. That console had no earned trophy, so reading earned trophies is
covered by the tests and by a patched copy of the real file, not yet by a trophy earned in a game.

## How it is put together

| File | What |
|---|---|
| `src/main.c` | Start-up; makes sure only one copy runs |
| `src/app.c` | Linking, syncing and the watch loop |
| `src/t2pd.c` | Reads `TRPTITLE.DAT` |
| `src/ucp.c` | Reads `TROPHY.UCP` |
| `src/http.c` | HTTPS client on BearSSL, with its own root certificates ([docs/CERTS.md](docs/CERTS.md)) |
| `src/json.c` | Reads the server's answers |
| `src/platform_ps5.c`, `src/platform_host.c` | The few things that differ between console and PC |

## Credits

- [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) by John Törnblom.
- [BearSSL](https://bearssl.org) by Thomas Pornin (MIT licence), in `third_party/BearSSL`.
- The meaning of the per-trophy record in `TRPTITLE.DAT` (earned flag and time) was cross-checked
  with [PS5-Suite](https://github.com/manos555555/PS5-Suite) (GPL v3).

## Licence

GPL v3 or later. See [LICENSE](LICENSE).
