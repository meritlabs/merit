
Debian
====================
This directory contains files used to package meritd/merit-qt
for Debian-based Linux systems. If you compile meritd/merit-qt yourself, there are some useful files here.

## merit: URI support ##


merit-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install merit-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your merit-qt binary to `/usr/bin`
and the `../../share/pixmaps/merit128.png` to `/usr/share/pixmaps`

merit-qt.protocol (KDE)

