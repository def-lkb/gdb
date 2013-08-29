#ifndef OCAML_SUPPORT_H
#define OCAML_SUPPORT_H

#include "defs.h"
#include "gdb_string.h"
#include "gdbtypes.h"
#include "symtab.h"
#include "expression.h"
#include "language.h"

typedef struct {
  void (*val_print) (struct type *type, const gdb_byte *valaddr,
                     int embedded_offset,
                     CORE_ADDR address, struct ui_file *stream,
                     int recurse, const struct value *val,
                     const struct value_print_options *options,
                     int depth);
} gdb_ocaml_support_t;

gdb_ocaml_support_t* ocaml_support_library(void);

int ocaml_support_val_print (struct type *type, const gdb_byte *valaddr,
                             int embedded_offset,
                             CORE_ADDR address, struct ui_file *stream,
                             int recurse, const struct value *val,
                             const struct value_print_options *options,
                             int depth);

#endif /*!OCAML_SUPPORT_H*/
