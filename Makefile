#
# Q'n'D makefile for SITGL
#

# path to the compiler
CC=C:\MinGW32\mingw32\bin\gcc.exe
# path to resource compiler
WINDRES=C:\MinGW32\mingw32\bin\windres.exe
LINKOPT=-shared -Wl,--dll -static-libgcc -s
LINKEXTRA=-lgdi32 -luser32 -lkernel32
LIB=SITGL.dll

CFLAGS=-Wall -Wdouble-promotion -Wshadow -Os -Wno-pointer-sign -DBUILDING_DLL -Wno-misleading-indentation
SOURCE=SIT.c SIT_App.c SIT_Button.c SIT_ComboBox.c SIT_CSSApply.c SIT_CSSLayout.c SIT_CSSParser.c SIT_Dialog.c SIT_EditBox.c SIT_Event.c SIT_Geometry.c SIT_Gradient.c SIT_Label.c SIT_ListBox.c SIT_Progress.c SIT_Render.c SIT_ScrollBar.c SIT_Slider.c SIT_StdDialog.c SIT_Tab.c SIT_Template.c SIT_ToolTip.c SIT_Widget.c UtilityLibLite.c nanovg.c
RES=SITGL.rc

OBJS=$(patsubst %.c,objs/%.o,$(SOURCE)) $(patsubst %.rc,objs/%.res,$(RES))

all: $(LIB)

objs/%.res: %.rc
	$(WINDRES) -i $< -J rc -o $@ -O coff 


objs/%.o: %.c
	$(CC) $< -c $(CFLAGS) -o $@

$(LIB): $(OBJS)
	$(CC) $(LINKOPT) $(OBJS) $(LINKEXTRA) -o $@

# create directory "objs" if it does not exist (quoted from make manual 4.3)
$(OBJS): | objs

objs:
	mkdir objs

clean:
	del objs\*.o objs\*.res $(LIB)
