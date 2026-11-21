# qutaq-flatpak

## Overview

`qutaq-flatpak` is a program for configuring access permissions for Flatpak applications

## Build

To build the program, be sure to install the libraries

**Arch Linux**

```sh
sudo pacman -S --needed base-devel cmake qt6-base qt6-tools flatpak
```

**Debian / Ubuntu**

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-tools-dev flatpak
```

**Fedora**

```sh
sudo dnf install gcc-c++ make cmake qt6-qtbase-devel qt6-qttools-devel flatpak
```

Building:
```sh
git clone https://github.com/Qutaq-Linux/qutaq-flatpak.git
cd qutaq-flatpak
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```
