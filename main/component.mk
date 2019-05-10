COMPONENT_PRIV_INCLUDEDIRS := . drivers fMSX odroidGo Z80 EMULib utils minIni video ugui
COMPONENT_SRCDIRS := . drivers fMSX odroidGo Z80 EMULib utils minIni video ugui
CPPFLAGS := -DBPS16 -DLSB_FIRST -DESP32
CFLAGS := -Ofast -mlongcalls -Wno-error
