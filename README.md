<p align="center">
  <img src="assets/minidock-appmark-v3.3.png" width="96" alt="MiniDock logo">
</p>

<h1 align="center">OPIA's MiniDock</h1>

<p align="center">A simple app that lets you put your favorite apps and tools on a dock.</p>

## How to build


![Windows](https://img.shields.io/badge/Windows-0078D4?style=flat-square)

Install [MSYS2](https://www.msys2.org/), then open the MINGW64 terminal and if not already install GCC
```sh
pacman -S --needed make python mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf mingw-w64-x86_64-qt6-base
```

Download the source-code and CD into it
```sh
make OS=Windows_NT
```
or just do make

![Linux](https://img.shields.io/badge/Linux-FCC624?style=flat-square&logo=linux&logoColor=black)

Assumes you have a Debian based OS
```sh
sudo apt install g++ make pkg-config python3 qt6-base-dev qt6-base-dev-tools
```

CD into where you installed the source-code
```sh
make
```

I didn't test on linux (ion use linux) so it might not work like windows

## How to use (without building)

I'm not making a Linux build since I don't use Linux, so sorry!

For Windows get it from [Releases](https://github.com/DingleberryWasHere/MiniDock/releases/tag/Releases).
