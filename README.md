# WHDUpdate

Keeps the WHDLoad games you already have up to date, on the Amiga itself.

This first version **only checks**. It changes nothing on your disks:

```
WHDUpdate Games:                     check the games under Games:
WHDUpdate ALL                        check every volume
WHDUpdate Games: SHOWALL             list every game, not just the ones to look at
WHDUpdate Games: REPORT=RAM:whd.txt  also write a full tab-separated list
WHDUpdate Games: OFFLINE             use the pack list already saved
WHDUpdate Games: FTP                 fetch the pack list by FTP
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
