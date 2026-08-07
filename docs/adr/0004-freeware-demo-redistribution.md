# ADR 0002: Whiplash playable demo sourcing and redistribution

- **Status:** Accepted
- **Date:** 2026-07-15
- **Drives:** WASM plan E3.S1, E3.S2, and E3.S3
- **Supersedes:** none
- **Superseded by:** none

## Context

The web build needs a reproducible demo data source so a first-time visitor can
play without supplying a retail CD image. The game data remains proprietary and
is not covered by ROLLER's source-code license. Calling the package a demo, or
finding it on a public download site, does not by itself establish a right to
mirror it, transform it, or distribute it with the web port.

E3.S1 therefore has two separate questions:

1. Is the selected archive an authentic publisher-distributed Whiplash demo?
1. Do the available terms authorize ROLLER to rehost or publicly serve its
   extracted data?

This ADR is a project risk decision based on the sources below, not legal
advice.

## Artifact identity

The selected artifact is the revised North American Whiplash playable demo.
Whiplash is the US title of Fatal Racing. The package is English-language and
uses the US data names (`CONFIG.USA`, `INGAME.USA`, and `SELECT.USA`). The
archive and bundled documentation do not state a semantic version number.

| Field                                          | Value                                                                   |
| ---------------------------------------------- | ----------------------------------------------------------------------- |
| Original filename                              | `whipdemo.zip`                                                          |
| Bundled title                                  | `Whiplash Playable Demo`                                                |
| Developer                                      | Gremlin Interactive Ltd.                                                |
| Publisher and distributor named by the package | Interplay Productions                                                   |
| Release description                            | Revised playable demo; finished game released in February 1996          |
| Archive size                                   | 5,398,262 bytes                                                         |
| SHA-256                                        | `eca6da4f64b97a400016ec8bd43fba713dcd237274d58f167d93e4149528414c`      |
| SHA-1                                          | `2c6a61f395dd064c8b749d93299393e48fecce0d`                              |
| MD5                                            | `eee3fb1c75e44e3d7515d2f3634d7b87`                                      |
| ZIP contents                                   | 206 files, 8,192,779 uncompressed bytes                                 |
| Demo data                                      | 190 files under `FATDATA`; `TRACK5.TRK` present and `TRACK1.TRK` absent |
| Bundled rights file                            | `READ.ME`, 5,805 bytes                                                  |
| `READ.ME` SHA-256                              | `273828c141673a636fa7a879fc4899d171cf20796a05e121e07b101df98a0f72`      |

The stable candidate source and mirror of record is:

```text
https://archive.org/download/WhiplashDemo/whipdemo.zip
```

Its item page and machine-readable metadata are:

- [Internet Archive item](https://archive.org/details/WhiplashDemo)
- [Internet Archive metadata](https://archive.org/metadata/WhiplashDemo)

The metadata records the file as an original Internet Archive upload made on
2012-07-08 by `swizzle@demu.org`. In that metadata, "original" describes the
file's role in the Archive item; it is not a claim that the uploader was the
publisher. The item has no `rights`, `licenseurl`, or `source` value. Its byte
size, MD5, and SHA-1 agree with the locally verified download. SHA-256 was
calculated locally because the item metadata does not provide one.

## Provenance evidence

An
[archived Interplay Whiplash product page](https://web.archive.org/web/19970415011215/http://www.interplay.com/games/whiplash.html)
linked the same filename from Interplay's own FTP service at:

```text
ftp://ftp1.interplay.com/pub/demos/whipdemo.zip
```

The page identified it as a 5.3 MB revised demo and documented how to extract
it. This is primary historical evidence that Interplay intentionally offered a
public Whiplash demo with this filename and approximate size. Wayback did not
preserve the FTP payload, so the official page alone cannot prove the current
mirror byte-for-byte. The selected archive's filename, size, internal title,
1996 Interplay and Gremlin notices, US data files, and bundled installer are
consistent with that official description.

Current third-party catalogs also carry an original archive with the same name
and displayed size. They corroborate availability but do not add rights:

- [DOS Games Archive](https://www.dosgamesarchive.com/download/whiplash/)
- [DOSGames.com](https://www.dosgames.com/game/whiplash/)
- [PCGamingWiki community file](https://community.pcgamingwiki.com/files/file/3445-whiplash-us-fatal-racing-eu-official-demo/)

No separate fatal.racing rights memorandum was found in the local `fatal-racing`
repository or the supplied planning documents. This ADR does not claim that such
approval exists.

## Rights assessment

The bundled `READ.ME` is the controlling evidence available with the artifact.
It describes a revocable, nonassignable license for personal, noncommercial home
entertainment. It permits no-cost copies to "friends and acquaintances." It
separately says public display of derivative works must be specifically
authorized by Interplay in writing, reserves all ungranted rights, and says the
permissions can be withdrawn.

That language establishes that the package was not an unrestricted freeware or
shareware release. It does not clearly authorize any of these planned actions:

- publishing the archive as a GitHub organization release asset;
- extracting and repackaging `FATDATA` into an Emscripten `.data` file;
- serving that package to the general public from `play.fatal.racing` or a
  Cloudflare preview;
- publicly displaying the game through a modified engine; or
- distributing the data alongside another product or service.

Interplay's historical public download proves authorized original distribution,
but it does not broaden the license text. The Internet Archive and other
third-party mirrors likewise do not supply authorization from the rights
holders.

## Decision

On 2026-07-15, after reviewing the provenance and license assessment above, the
project owner explicitly directed the project to proceed with public display of
the demo. The project accepts the risk of using the historical noncommercial
demo grant for this no-cost browser deployment. This is a project decision, not
a claim that the Internet Archive supplied a license or that new written
rights-holder permission was obtained.

Use the hash-pinned Internet Archive URL above as the build source and mirror of
record. E3.S2 and CI may download that exact archive, verify its byte size and
SHA-256, extract its demo `FATDATA`, and package the result for E3.S3. E3.S3 may
serve the packaged data at no cost through the ROLLER browser build on
fatal.racing, Cloudflare production, and branch or pull-request previews.

Do not commit `whipdemo.zip` or extracted game data to ROLLER. Do not create a
separate GitHub organization release asset for the raw ZIP while the stable
Internet Archive source remains available. The deployed Emscripten `.data`
package is the only approved rehosted form and must contain only the selected
demo `FATDATA` required by the browser build.

The public page must identify the content as the Whiplash playable demo, credit
Gremlin Interactive and Interplay Productions, link to the source artifact, and
state that the game data is proprietary and separate from ROLLER's source-code
license. Deployment must remain no-cost and noncommercial.

Any rights-holder objection or withdrawal notice immediately suspends new
publishing and triggers removal of the deployed demo data while the project
reviews the request. Any later written authorization and its conditions must be
added to this ADR or a superseding ADR.

## Integrity and fallback policy

Acquisition must verify both the exact byte count and SHA-256 before opening the
ZIP. A mismatch is a hard failure. The tooling must not silently accept a new
hash, a repackaged DOSBox bundle, a cover-disc image, or a `latest` URL.

If the selected URL is temporarily unavailable, a cached copy may be used only
when it matches the identity above. A fallback mirror may be added only after
the downloaded bytes match the same SHA-256. If no hash-identical source
remains, acquisition stops until this ADR is updated with new provenance,
content comparison, rights review, byte size, and SHA-256.

The raw archive is not promoted to an organization release asset under this
decision. Link availability or long-running third-party hosting is not treated
as additional permission.

## Update procedure

1. Preserve this artifact identity; never overwrite it in place.
1. Record any written rights authorization, objection, or withdrawal and confirm
   its effect on every item in the Decision section.
1. If the artifact changes, inspect the new archive and bundled terms, compare
   its manifest with this revision, and assign a new immutable identity.
1. Review and accept a new or superseding ADR before changing build or deploy
   inputs.
1. Keep all downloaded archives and extracted game data outside tracked source.

## Consequences

- E3.S1 now has a reproducible artifact identity, stable candidate URL, and
  documented provenance.
- The project does not infer redistribution rights from the words "demo" or
  "freeware."
- Local and CI E3.S2 acquisition can proceed without committing game data.
- E3.S3 may package and deploy the demo for no-cost public browser play under
  the conditions above.
- The raw ZIP remains at the hash-pinned Internet Archive source rather than a
  new organization release asset.
- Retail user import remains a separate E4 path and is not decided by this ADR.
