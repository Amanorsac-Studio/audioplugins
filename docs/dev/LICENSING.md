# Licensing

Status: **built but not armed.** The code is in the repository and compiles into
every product, but enforcement is switched off. Nothing about the current builds
depends on a licence.

To arm it at release, set `AMANORSAC_LICENSING_ENABLED=1` in `CMakeLists.txt`
(currently line 136). That is the only switch.

## Why it is off

With enforcement live, any machine without a licence file drops into demo
behaviour and dips the audio every twenty seconds. That would sabotage our own
listening tests while the products are still being built. It is armed once, at
release.

## What exists today

| Piece | File | State |
| --- | --- | --- |
| Machine fingerprint | `src/common/licensing/MachineId.*` | done, tested |
| Licence verification | `src/common/licensing/LicenseManager.*` | done, tested |
| Licence generator (studio only) | `tools/licensegen/main.cpp` | done, tested |
| Audio-path entanglement | `PluginProcessor::processBlock` | written, gated off |
| Activation window | — | **not started** |
| Installer / purchase flow | — | **not started** |

## How it works

A licence is a short text payload signed with the studio's RSA private key. The
product embeds only the public key, so a licence cannot be forged without the
private key. There is no key generator to write, only a binary to patch.

```
product: AMANORSAC-SUITE-1
machine: Z7PPR-674N2-SXREF-SY62Q
licensedto: Jane Roe
email: jane@example.com
order: INV-1043
slot: 1
issued: 2026-09-05
expires: never
SIGNATURE: <rsa signature over the payload above>
```

The machine ID hashes the Windows MachineGuid, the C: volume serial, the JUCE
device ID and CPU details together. Several weak identifiers rather than one
means a licence survives a new network adapter or an added drive, but will not
verify on a different computer. It prints in a Crockford-style alphabet with no
I, L, O or U so a customer cannot mistype it into an email.

Installed licence location:
`%APPDATA%\Amanorsac Studio\licence.amanorsac`

## Verified behaviour

| Case | Result |
| --- | --- |
| Genuine licence, correct machine | accepted |
| Payload edited, signature retained | rejected, signature fails |
| Licence issued to another machine | rejected, wrong computer |
| Signature replaced with a forgery | rejected |

## Issuing a licence

The generator is a studio tool. It is `EXCLUDE_FROM_ALL`, so it is never built
into a customer package.

```
cmake --build build/win-x64 --config Release --target AmanorsacLicenseGen
```

Run it from the key directory:

```
AmanorsacLicenseGen --machine-id
AmanorsacLicenseGen --issue --machine <customer id> --name "Jane Roe" \
                    --email jane@example.com --order INV-1043 --slot 1
AmanorsacLicenseGen --verify <file>
```

Two activations per purchase (desktop and laptop) was the decision. Because
licences are offline, the limit is enforced by us at issue time: issue at most
two, `--slot 1` and `--slot 2`. Keep a record of issued slots per order.

## The private key

`C:\Users\amano\Documents\Amanorsac Studio\Amanorsac-Licensing-Keys\amanorsac-private.key`

Outside the repository by design. `.gitignore` blocks `*.key` and `*.amanorsac`.

- **Back it up offline.** Lose it and no licence can ever be reissued; every
  existing customer would need a new build with a new public key.
- **Never commit or send it.** Anyone holding it can mint licences.

The matching public key is embedded in `LicenseManager.cpp` and is not secret.

## What this does and does not achieve

It stops casual sharing completely. A licence file emailed to a friend will not
verify on their machine, and nobody can generate licences without the private
key. That is where the revenue protection actually is.

It does not make the product uncrackable, and no client-side scheme can. The
code runs on the customer's CPU, under their debugger. A determined attacker can
patch the check out. The design raises that cost — the entitlement value feeds a
gain ramp consumed by the audio path rather than gating a boolean, so a careless
patch leaves the product audibly wrong instead of unlocked — but it does not
eliminate it. Anyone claiming otherwise about any scheme is mistaken.

If stronger protection is ever needed, iLok/PACE is the industry answer, and it
is hardware-backed and professionally maintained. It costs money per year and
per activation, and adds customer friction.

## Remaining work, in order

1. **Activation window.** Customers currently have no way to install a licence
   short of copying a file into `%APPDATA%` by hand. Needs: the machine ID shown
   with a copy button, a file picker or paste box, the precise rejection reason
   from `statusMessage()`, and current status. Reachable from the top-bar menu.
2. **Demo state in the UI.** An unobtrusive but honest indication that the
   product is unlicensed, so nobody thinks the audio dip is a bug.
3. **Installer.** Ship the licence folder, and decide whether the installer
   offers activation at the end.
4. **Purchase flow.** How the customer's machine ID reaches us and the licence
   reaches them. Manual email works to start; automate later.
5. **Arm it.** Flip `AMANORSAC_LICENSING_ENABLED` to 1, rebuild, and re-run the
   four verification cases against a shipping build before release.
