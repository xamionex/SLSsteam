# **SLSsteam - Steamclient Modification for Linux**

## Index

1. [Downloading and Compiling](#downloading-and-compiling)
2. [Usage](#usage)
3. [Configuration](#configuration)
4. [Installation and Uninstallation](#installation-and-uninstallation)
5. [Updating](#updating)
6. [Credits](#credits)

## Downloading and Compiling

It is recommended to grab a release from
[Releases](https://github.com/AceSLS/SLSsteam/releases) instead!
Afterwards skip straight to [Usage](#usage) or [Installation and Uninstallation](#installation-and-uninstallation)

Make sure you have OpenSSL installed!
Then run:

```bash
git clone "https://github.com/AceSLS/SLSsteam"
cd SLSsteam
make
```

## Usage

```bash
LD_AUDIT="/full/path/to/SLSsteam.so" steam
```

## Configuration

Configuration gets created at ~/.config/SLSsteam/config.yaml during first run

## Installation and Uninstallation

```bash
./setup.sh install
./setup.sh uninstall
```

## Updating

```bash
git pull
make rebuild
```

Afterwards run the installer again if that's what you've been using to launch SLSsteam

## Credits

- Riku_Wayfinder: Being extremely supportive and lightening my workload by a lot.
  So show him some love my guys <3
- thismanq: Informing me that DisableFamilyShareLockForOthers is possible
- Gnanf: Helping me test the Family Sharing bypass
- rdbo: For his great libmem library, which saved me a
  lot of development and learning time
- oleavr and all the other awesome people working on Frida
  for easy instrumentation which helps a lot in analyzing, testing and debugging
- All the folks working on Ghidra,
  this was my first project using it and I'm in love with it!
