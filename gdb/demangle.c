/* Basic C++ demangling support for GDB.

   Copyright (C) 1991-2013 Free Software Foundation, Inc.

   Written by Fred Fish at Cygnus Support.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


/*  This file contains support code for C++ demangling that is common
   to a styles of demangling, and GDB specific.  */

#include "defs.h"
#include "command.h"
#include "gdbcmd.h"
#include "demangle.h"
#include "gdb-demangle.h"
#include "gdb_string.h"

/* Select the default C++ demangling style to use.  The default is "auto",
   which allows gdb to attempt to pick an appropriate demangling style for
   the executable it has loaded.  It can be set to a specific style ("gnu",
   "lucid", "arm", "hp", etc.) in which case gdb will never attempt to do auto
   selection of the style unless you do an explicit "set demangle auto".
   To select one of these as the default, set DEFAULT_DEMANGLING_STYLE in
   the appropriate target configuration file.  */

#ifndef DEFAULT_DEMANGLING_STYLE
#define DEFAULT_DEMANGLING_STYLE AUTO_DEMANGLING_STYLE_STRING
#endif

/* See documentation in gdb-demangle.h.  */
int demangle = 1;

static void
show_demangle (struct ui_file *file, int from_tty,
	       struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file,
		    _("Demangling of encoded C++/ObjC/OCaml names "
		      "when displaying symbols is %s.\n"),
		    value);
}

/* See documentation in gdb-demangle.h.  */
int asm_demangle = 0;

static void
show_asm_demangle (struct ui_file *file, int from_tty,
		   struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file,
		    _("Demangling of C++/ObjC/OCaml names in "
		      "disassembly listings is %s.\n"),
		    value);
}

/* String name for the current demangling style.  Set by the
   "set demangle-style" command, printed as part of the output by the
   "show demangle-style" command.  */

static char *current_demangling_style_string;

/* The array of names of the known demanglyng styles.  Generated by
   _initialize_demangler from libiberty_demanglers[] array.  */

static const char **demangling_style_names;
static void
show_demangling_style_names(struct ui_file *file, int from_tty,
			    struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("The current C++ demangling style is \"%s\".\n"),
		    value);
}

/* Set current demangling style.  Called by the "set demangle-style"
   command after it has updated the current_demangling_style_string to
   match what the user has entered.

   If the user has entered a string that matches a known demangling style
   name in the demanglers[] array then just leave the string alone and update
   the current_demangling_style enum value to match.

   If the user has entered a string that doesn't match, including an empty
   string, then print a list of the currently known styles and restore
   the current_demangling_style_string to match the current_demangling_style
   enum value.

   Note:  Assumes that current_demangling_style_string always points to
   a malloc'd string, even if it is a null-string.  */

static void
set_demangling_command (char *ignore, int from_tty, struct cmd_list_element *c)
{
  const struct demangler_engine *dem;

  /*  First just try to match whatever style name the user supplied with
     one of the known ones.  Don't bother special casing for an empty
     name, we just treat it as any other style name that doesn't match.
     If we match, update the current demangling style enum.  */

  for (dem = libiberty_demanglers; 
       dem->demangling_style != unknown_demangling; 
       dem++)
    {
      if (strcmp (current_demangling_style_string,
		  dem->demangling_style_name) == 0)
	{
	  current_demangling_style = dem->demangling_style;
	  break;
	}
    }

  /* Check to see if we found a match.  If not, gripe about any non-empty
     style name and supply a list of valid ones.  FIXME: This should
     probably be done with some sort of completion and with help.  */

  if (dem->demangling_style == unknown_demangling)
    {
      if (*current_demangling_style_string != '\0')
	{
	  printf_unfiltered (_("Unknown demangling style `%s'.\n"),
			     current_demangling_style_string);
	}
      printf_unfiltered (_("The currently understood settings are:\n\n"));
      for (dem = libiberty_demanglers; 
	   dem->demangling_style != unknown_demangling; 
	   dem++)
	{
	  printf_unfiltered ("%-10s %s\n", dem->demangling_style_name,
			     dem->demangling_style_doc);
	  if (dem->demangling_style == current_demangling_style)
	    {
	      xfree (current_demangling_style_string);
	      current_demangling_style_string =
		xstrdup (dem->demangling_style_name);
	    }
	}
      if (current_demangling_style == unknown_demangling)
	{
	  /* This can happen during initialization if gdb is compiled with
	     a DEMANGLING_STYLE value that is unknown, so pick the first
	     one as the default.  */
	  current_demangling_style = libiberty_demanglers[0].demangling_style;
	  current_demangling_style_string =
	    xstrdup (libiberty_demanglers[0].demangling_style_name);
	  warning (_("`%s' style demangling chosen as the default."),
		   current_demangling_style_string);
	}
    }
}

/* See documentation in gdb-demangle.h.  */

void
set_demangling_style (char *style)
{
  if (current_demangling_style_string != NULL)
    {
      xfree (current_demangling_style_string);
    }
  current_demangling_style_string = xstrdup (style);
  set_demangling_command ((char *) NULL, 0, (struct cmd_list_element *) NULL);
}

/* G++ uses a special character to indicate certain internal names.  Which
   character it is depends on the platform:
   - Usually '$' on systems where the assembler will accept that
   - Usually '.' otherwise (this includes most sysv4-like systems and most
     ELF targets)
   - Occasionally '_' if neither of the above is usable

   We check '$' first because it is the safest, and '.' often has another
   meaning.  We don't currently try to handle '_' because the precise forms
   of the names are different on those targets.  */

static char cplus_markers[] = {'$', '.', '\0'};

/* See documentation in gdb-demangle.h.  */

int
is_cplus_marker (int c)
{
  return c && strchr (cplus_markers, c) != NULL;
}

extern initialize_file_ftype _initialize_demangler; /* -Wmissing-prototypes */

void
_initialize_demangler (void)
{
  int i, ndems;

  /* Fill the demangling_style_names[] array.  */
  for (ndems = 0;
       libiberty_demanglers[ndems].demangling_style != unknown_demangling; 
       ndems++)
    ;
  demangling_style_names = xcalloc (ndems + 1, sizeof (char *));
  for (i = 0;
       libiberty_demanglers[i].demangling_style != unknown_demangling; 
       i++)
    demangling_style_names[i] =
      xstrdup (libiberty_demanglers[i].demangling_style_name);

  add_setshow_boolean_cmd ("demangle", class_support, &demangle, _("\
Set demangling of encoded C++/ObjC/OCaml names when displaying symbols."), _("\
Show demangling of encoded C++/ObjC/OCaml names when displaying symbols."), NULL,
			   NULL,
			   show_demangle,
			   &setprintlist, &showprintlist);

  add_setshow_boolean_cmd ("asm-demangle", class_support, &asm_demangle, _("\
Set demangling of C++/ObjC/OCaml names in disassembly listings."), _("\
Show demangling of C++/ObjC/OCaml names in disassembly listings."), NULL,
			   NULL,
			   show_asm_demangle,
			   &setprintlist, &showprintlist);

  /* FIXME: cagney/2005-02-20: The code implementing this variable are
     malloc-ing and free-ing current_demangling_style_string when it
     should instead just point to an element of
     demangling_style_names.  */
  add_setshow_enum_cmd ("demangle-style", class_support,
			demangling_style_names,
			(const char **) &current_demangling_style_string, _("\
Set the current C++ demangling style."), _("\
Show the current C++ demangling style."), _("\
Use `set demangle-style' without arguments for a list of demangling styles."),
			set_demangling_command,
			show_demangling_style_names,
			&setlist, &showlist);

  /* Set the default demangling style chosen at compilation time.  */
  set_demangling_style (DEFAULT_DEMANGLING_STYLE);
}
