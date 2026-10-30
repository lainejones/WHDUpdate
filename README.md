# WHDUpdate

Keeps the WHDLoad games you already have up to date, on the Amiga itself.

It checks first and changes nothing unless you ask it to update:

```
WHDUpdate Games:                     check the games under Games:
WHDUpdate ALL                        check every volume
WHDUpdate Games: SHOWALL             list every game, not just the ones to look at
WHDUpdate Games: REPORT=RAM:whd.txt  also write a full tab-separated list
WHDUpdate Games: OFFLINE             use the pack list already saved
WHDUpdate Games: FTP                 fetch the pack list by FTP
WHDUpdate Games: UPDATE              update the games with a newer pack, asking for each
WHDUpdate Games: UPDATE YES          ... without asking
WHDUpdate Games: GAME=Lotus#?        only the games whose drawer name fits the pattern
DEPTH=n                              how many drawer levels to look down (3)
```

It reads Retroplay's list of WHDLoad packs (the DAT) from Turran's pack server
(`ftp2.grandis.nu`, plain HTTP, falling back to FTP). The list is saved in
`PROGDIR:Cache` and only downloaded again when the server has a newer one.
It then finds every game drawer (a drawer with a WHDLoad slave in it), reads
the version from the slave, and works out which pack the drawer came from.

Each game gets one of these:

| | |
|---|---|
| `current` | the newest pack has the version you have |
| `UPDATE` | there is a newer pack |
| `check` | the versions can be read either way (`1.51` vs `1.6`, `1.10` vs `1.7`): look yourself |
| `unknown` | the slave doesn't say its version |
| `no-match` | no pack in the list fits this drawer |
| `newer` | yours is newer than the newest pack |

The return code is 5 (WARN) when there are updates.

## Updating

For each game you say yes to, WHDUpdate:

1. downloads the pack into `<volume>:WHDUpdate-Old/.work/` and checks its size and CRC
   against the pack list;
2. unpacks it there with `C:LhA` (`C:UNLZX` for the few `.lzx` packs);
3. copies your saves and high scores from the old drawer into the new one - the ones
   the pack doesn't have, and the ones it has fresh (empty high scores): yours win;
4. gives the new icons the places of the old ones and keeps the tooltypes you added or
   changed (`CUSTOM1`..., `PRELOAD`, ...) - with OS 3.5+ icon.library only, as an older
   one would lose a colour icon's image when writing it back;
5. moves the old drawer and its icon to `<volume>:WHDUpdate-Old/` and the new one into
   its place, under the old name. If that fails, the old one goes back.

Nothing changes until step 5, and the old copy stays in `WHDUpdate-Old` until you delete
it. Every update is logged in `PROGDIR:WHDUpdate.log`. Downloads are one at a time with a
pause between them, to go easy on the server.

Needs Kickstart 2.04 or newer, any 680x0, and a TCP/IP stack with
`bsdsocket.library` (not for `OFFLINE`).

## Building

In WSL with Bebbo's amiga-gcc on the PATH:

```
make          # out/WHDUpdate
make test     # host checks of the slave, DAT, inflate and matching code
```

## Licence

GPL-3.0-only.
