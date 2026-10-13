BLOCKSDS	?= /opt/blocksds/core

NAME		:= blockcraft
GAME_TITLE	:= BlockCraft DS
GAME_SUBTITLE	:= Build, mine, survive
GAME_AUTHOR	:= Homebrew
GAME_ICON	:= gfx/icon.gif

SOURCEDIRS	:= source
BINDIRS		:= data
CFLAGS		:= -Wno-unused-function

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
