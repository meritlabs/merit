#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

MERITD=${MERITD:-$SRCDIR/meritd}
MERITCLI=${MERITCLI:-$SRCDIR/merit-cli}
MERITTX=${MERITTX:-$SRCDIR/merit-tx}
MERITQT=${MERITQT:-$SRCDIR/qt/merit-qt}

[ ! -x $MERITD ] && echo "$MERITD not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
MRTVER=($($MERITCLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for meritd if --version-string is not set,
# but has different outcomes for merit-qt and merit-cli.
echo "[COPYRIGHT]" > footer.h2m
$MERITD --version | sed -n '1!p' >> footer.h2m

for cmd in $MERITD $MERITCLI $MERITTX $MERITQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${MRTVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${MRTVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
