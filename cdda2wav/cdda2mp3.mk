#ident "@(#)cdda2mp3.mk	1.2 10/02/11 "
###########################################################################
SRCROOT=	..
RULESDIR=	RULES
include		$(SRCROOT)/$(RULESDIR)/rules.top
###########################################################################

INSDIR=		bin
INSMODE=	755
TARGET=		cdda2mp3
SCRFILE=	cdda2mp3
XMK_FILE=	cdda2mp3.mk1

###########################################################################
include		$(SRCROOT)/$(RULESDIR)/rules.scr
###########################################################################
