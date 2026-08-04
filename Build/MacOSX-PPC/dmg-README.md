# GTetrinet

A TetriNET client. This release is a self-contained app -- everything it
needs (SDL/glib libraries, themes, fonts, icons) is bundled inside
`GTetrinet.app`, so no Homebrew/Tigerbrew or any other install step is
required.

## Launching

Mount the `.dmg` and drag `GTetrinet.app` wherever you like (or run it
straight from the mounted disk image), then double-click it like any
other Mac app.

To pass command-line options, either:

- Launch from Terminal: `open -a GTetrinet.app --args -c irc.server.com -n MyNick`
- Or run the binary inside the bundle directly:
  `GTetrinet.app/Contents/MacOS/gtetrinet -c irc.server.com -n MyNick`

## Command-line options

| Flag | Long form | Description |
|---|---|---|
| `-c <host>` | `--connect <host>` | Connect to a TetriNET server on launch |
| `-n <name>` | `--nickname <name>` | Nickname to use |
| `-t <team>` | `--team <team>` | Team name |
| `-s` | `--spectate` | Join as a spectator |
| `-p <password>` | `--password <password>` | Server/spectator password |

All of these can also be set from the in-app Connect dialog -- the
command-line flags are just a shortcut for connecting immediately on
launch (useful for a desktop shortcut pointed at a specific server, for
example).

## Platforms

- **GTetrinet-macOS.dmg**: modern macOS (Intel and Apple Silicon).
- **GTetrinet-OSX-PPC.dmg**: PowerPC Macs running Mac OS X 10.4 "Tiger"
  (and likely later PPC releases, though only tested on Tiger). Same
  `GTetrinet.app`, built natively for PowerPC.

Both `.dmg` files use an HFS+/Apple Partition Map image format
specifically so they mount correctly on real PPC/Tiger hardware, not
just modern macOS.
