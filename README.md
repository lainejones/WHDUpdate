# WHDUpdate

Keeps the WHDLoad games you already have up to date, on the Amiga itself.

It checks first and changes nothing unless you ask it to update:

```
WHDUpdate Games:                     check the games under Games:
WHDUpdate ALL                        check every volume
WHDUpdate Games: SHOWALL             list every game, not just the ones to look at
WHDUpdate Games: REPORT=RAM:whd.txt  also write a full tab-separated list
WHDUpdate Games: REPORT=RAM:whd.csv  ... or every game as CSV (see Save list as CSV)
WHDUpdate Games: OFFLINE             use the pack list already saved
WHDUpdate Games: FTP                 fetch the pack list by FTP
WHDUpdate Games: UPDATE              update the games with a newer pack, asking for each
WHDUpdate Games: UPDATE YES          ... without asking
WHDUpdate Games: GAME=Lotus#?        only the games whose drawer name fits the pattern
DEPTH=n                              how many drawer levels to look down (3)
```

## The window

Double-click the icon (or `WHDUpdate GUI` in a Shell): type the drawers your games are
in - several with `;` between them, or `ALL` - and press **Check**. Type part of a name in **Find** and
the list shows only those games. Click a game to mark it (`*`) - the game you clicked
stays highlighted - or **Mark all** (the games the list shows), then **Update marked**. To mark
a run of games, drag over them with the button held, or click the first and Shift-click the
last; the status line says how many are marked. **Show** picks what the list shows:
the updates, all games, the ones to check by hand, the ones not found, the ones whose
slave has no version, the packs you don't have (**Missing**, see Installing missing games),
the games you don't want (**Not wanted**), the ones this machine can't run (**Won't run here**)
or the games you have more than once (**Duplicates**) - the last three are for removing, see below. **Stop** ends a check or the updates between two steps. The drawers
are remembered (in `ENVARC:WHDUpdate.prefs` with the other settings, saved when
you press Check and when the window closes), and the next start checks right away.
What a check read from each game drawer is kept in `PROGDIR:Cache/games.txt` with the
drawer's date, so the next check looks inside a drawer again only when something in it
was added, replaced or removed.

### Saving the list

**Project > Save list as CSV** writes the games the list shows (pick the view first: **All
games** for everything) to a file for a spreadsheet: name, pack set, status, the version you
have and the newest, the pack file, its languages and systems, the drawer, the pack's size and
CRC, and what the slave asks of the machine - its WHDLoad version, chip and expansion memory,
CPU, chipset and Kickstart image - with what this machine lacks of it and the number of saves
(when Duplicates counted them).

### Installing missing games

**Show Missing** lists the packs of the sets in use that none of your drawers has. Mark the
ones you want and press **Install marked**: each pack is downloaded, checked against the pack
list and unpacked like an update, and goes into the first games drawer of the settings - into
its letter drawer (`A`, `B` ... `0-9`) when the drawer has those. Demos and magazines go into
their own drawers from **Settings**. The drawer gets an `ArchiveName.txt` naming the pack.

### Backing up saves

**Project > Back up saves** copies the save games and high scores of the games the list shows
(**All games** for all of them) into a drawer you pick, as `Saves-<date>/<game>/...`, in the
same drawers as in the game. Disk images (`Disk.1` ...) are left out: in most packs they are the
game itself, and a game that saves onto its disk keeps that inside the image.

### Removing games you don't want

**Show Not wanted** lists the installed games whose pack is of a language or a system
that is turned off in **Settings > Languages** and **Settings > Systems**: the AGA games
on an ECS machine once AGA is off, the German and French versions when only English is
on, the CD32 versions, and so on. A pack in several languages is wanted while one of them
is on; a pack for several systems (AGA and NTSC) is not wanted when any of them is off.
Click the games to remove, or **Mark all**, then **Remove marked** (also in the Project
menu). It asks first, and offers two ways:

- **Delete**: the game drawers and their icons are deleted for good.
- **Move**: they go to `<volume>:WHDUpdate-Old/`, like an updated game's old copy. That is
  quick, but frees no room until you delete them from there. The drawer gets the default
  drawer icon, so Workbench shows it in the volume's window.

**Show Won't run here** lists the games whose slave asks for more than this machine has:
the AGA chipset, a 68020, more chip memory than the machine has, expansion memory it can't do
without, a Kickstart image that is not in `DEVS:Kickstarts`, or a newer WHDLoad than `C:WHDLoad`.
The last two can be put right (`needs`); the others are the machine's (`won't run`). The
summary line shows what WHDUpdate found the machine to have.

**Show Duplicates** lists the games installed more than once - a drawer of the same name in
two places (`Games:A/Zool` and `Games2:Z/Zool`) - a group after the other, with the drawer
each copy is in and how many save and high score files it has (`sv`). **Mark all** marks every copy but the
best of each group: the newest version, then the one with the most saves.

Each removal is written to `PROGDIR:WHDUpdate.log`. The marks of these three views are kept
apart from the update marks, so marking games to update never removes them.

The window is laid out from the screen font and fits a 640x200 NTSC screen with Topaz 8;
when the screen font is too big for the screen, it uses Topaz 8. The list always uses the
fixed-width system font, so its columns line up.

## How it works

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
2. unpacks it there with `C:LhA` (`C:UNLZX` for the few `.lzx` packs); if the new version
   needs a Kickstart image that is not in `DEVS:Kickstarts` and the one you have didn't, it
   asks first (**Update anyway** or **Keep the old one**; in a Shell it asks with the update,
   and with `YES` it goes ahead and says so);
3. copies your saves and high scores from the old drawer into the new one - the ones
   the pack doesn't have, and the ones it has fresh (empty high scores): yours win. A disk
   image (`Disk.1` ...) the new pack brings is its data and is never overwritten; one only
   the old drawer has (a save disk the game made) is carried over;
4. gives the new icons the places of the old ones and keeps the tooltypes you added or
   changed (`CUSTOM1`..., `PRELOAD`, ...) - with OS 3.5+ icon.library only, as an older
   one would lose a colour icon's image when writing it back;
5. moves the old drawer and its icon to `<volume>:WHDUpdate-Old/` and the new one into
   its place, under the old name. If that fails, the old one goes back.

Nothing changes until step 5, and the old copy stays in `WHDUpdate-Old` until you delete
it. **Project > Undo update** puts it back: click the game, pick it, and the copy from before
its last update returns, with the saves made since; the newer one goes to `WHDUpdate-Old` as
`<name>.undone`. Before an update starts, the requester (or the Shell) says about how much
room the new versions take on each volume. Every update is logged in `PROGDIR:WHDUpdate.log`, and the drawer gets an `ArchiveName.txt`
naming the pack (as WHDFetch writes it), which later checks trust over the slave's version. Downloads are one at a time with a
pause between them, to go easy on the server.

Needs Kickstart 2.04 or newer, any 680x0, and a TCP/IP stack with
`bsdsocket.library` (not for `OFFLINE`).

## Installing

Unpack the archive and double-click **Install_WHDUpdate** in the `WHDUpdate` drawer. It asks
where the program's drawer should go (`SYS:Tools` unless you pick another), makes it with your
Workbench's own drawer icon, and copies WHDUpdate, its icon, this README and the licence there.
Running it again updates an installed copy; your settings are kept. WHDUpdate also runs
straight from the unpacked drawer.

## Building

In WSL with Bebbo's amiga-gcc on the PATH:

```
make          # out/WHDUpdate
make test     # host checks of the slave, DAT, inflate and matching code
```

## Licence

GPL-3.0-only.
