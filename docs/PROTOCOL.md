# What the payload says to the server

All calls are HTTPS with JSON bodies. The default server is the one compiled in
(`API_URL` in the Makefile). To use another, write its address in `/data/trophyshare/api`.

The payload sends: trophy set IDs, the IDs and times of earned trophies, the name of the console
user (only as the label of the link, for example "PS5 - Bob"), and, once per trophy set the
server has never seen, that set's `tropconf.json` and one `tropmeta` file. Nothing else.

## Linking

The console has no key yet, so it asks for a code and shows it on screen.

```
POST /v1/link/start      { "label": "PS5 - Bob" }
200  { "code": "7K3M9QXZ", "secret": "...", "show": "7K3M-9QXZ",
       "url": "https://.../link/", "expires_in": 600, "interval": 5 }
```

The player types the code on the site while signed in. Meanwhile the console asks, every
`interval` seconds:

```
POST /v1/link/poll       { "code": "7K3M9QXZ", "secret": "..." }
200  { "status": "pending" }
200  { "status": "linked", "key": "tsk_...", "player": { "id": "...", "nickname": "..." } }
404 / 410                the code is unknown or has run out
```

The key is saved in `/data/trophyshare/keys/<user id>` and sent with every later call as
`Authorization: Bearer tsk_...`. Only the console that asked for the code knows the `secret`,
so nobody else can collect the key.

A key can also be made by hand on the site and saved as `/data/trophyshare/key`. The payload
gives it to the user who is signed in on the console and removes the loose file.

## Sending trophies

```
POST /v1/sync
{ "v": 1, "sets": [ { "np": "NPWR12345_00", "rev": 1746241, "earned": [[1, 1790000000], [4, 0]] } ] }

200  { "need": ["NPWR12345_00"], "need_icon": [], "stored": 0, "new_trophies": 0,
       "player": { "id": "...", "nickname": "..." }, "totals": { "p": 0, "g": 1, "s": 0, "b": 2, "games": 1 },
       "url": "https://.../u/<player id>" }
401  the key was removed on the site: the payload deletes it and offers a new code
```

`earned` is a list of `[trophy ID, Unix time]`; time 0 means unknown. `rev` is
`trophyDefinitionRevision` from `tropconf.json`.

`need` lists trophy sets the server cannot store yet because it does not know them (or knows an
older revision). For each one the payload sends the definitions, then syncs those sets again:

```
PUT /v1/games/NPWR12345_00
{ "lang": "en-US", "conf": <tropconf.json as it is>, "meta": <tropmeta_en-US.json as it is> }
```

## When it runs

The payload stays in memory. Every 20 seconds it looks at the size and date of each
`TRPTITLE.DAT`; when one changed it syncs again. It also syncs every 6 hours regardless.
Sending the payload again replaces the copy that is running. `ps5-trophy-share-stop.elf` stops it.
