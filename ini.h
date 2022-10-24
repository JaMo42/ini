#ifndef INI_H
#define INI_H
#include <stdio.h>
#include <stdbool.h>
#include "rb_tree.h"

enum {
  /// Allow global properties, these are properties that occurr before any
  /// section, or that re within an unnamed section ("[]").
  INI_GLOBAL_PROPS = 0x1,

  /// Allow nested sections using `section_delim` as path delimiters.
  INI_NESTING = 0x2,

  /// Allow comments that do not start at the beginning of a line.
  /// If they are after value there has to be at least one whitespace between
  /// the value and the comment character.
  INI_INLINE_COMMENTS = 0x4,

  /// Allow for values to use quoted strings, these can contain escape
  /// sequences to represent some special characters.
  INI_QUOTED_VALUES = 0x8,

  INI_ALL_FLAGS = 0x10 - 1
};

/// Parsing options to specify which features to use.
/// The global value `ini_options_stable` specifies only the stable options.
/// The macro `INI_OPTIONS_WITH_FLAGS(flags)` can be used to create options
/// that only want custom flags and not change any of the characters.
///
/// The flags are: `INI_GLOBAL_PROPS`, `INI_NESTING`, `INI_INLINE_COMMENTS`,
///                `INI_QUOTED_VALUES`.
/// `INI_ALL_FLAGS` enables all falgs.
typedef struct {
  unsigned char flags;
  char name_value_delim;
  char comment_char;
  char section_delim;
} Ini_Options;

/// The stable features:
///  - none of the flags are enabled
///  - `=` is the name-value delimiter
///  - `;` is the comment character
///  - `.` is the section delim, this is not actually used as nesting is not
///        enabled but it is set anyways so this value can be copied and have
///        all the defaults set.
extern const Ini_Options ini_options_stable;

/// Creates options with custom flags. The characters are copied from
/// `ini_options_stable`.
#define INI_OPTIONS_WITH_FLAGS(flags_)                       \
  ((Ini_Options) {                                           \
    .flags = (flags_),                                       \
    .name_value_delim = ini_options_stable.name_value_delim, \
    .comment_char = ini_options_stable.comment_char,         \
    .section_delim = ini_options_stable.section_delim,       \
  })

/// A single ini section.
typedef struct {
  struct rbtree values;
  struct rbtree tables;
} Ini_Table;

/// The ini object.
typedef struct {
  Ini_Table tables_and_globals;
  Ini_Options options;
} Ini;

/// The result of parsing an ini file.
///
/// If the parsing was successful `unwrap` contains the full object and `ok` is
/// set to `true`. `error` is also set to `"Success"` in this case.
///
/// If the was an error during parsing `ok` is set to `false` and `error`
/// contains a description of the error. In this case the ini object is already
/// free'd.
typedef struct {
  Ini unwrap;
  const char *error;
  bool ok;
} Ini_Parse_Result;

/// A value string.
///
/// The `data` member is null terminated but since quoted values may also
/// contain null bytes the size of the string is recorded as well.
typedef struct {
  char *data;
  size_t size;
} Ini_String;

/// Parses an ini file from a string.
///
/// If length is `0` it is parsed until a null terminator.
Ini_Parse_Result ini_parse_string (const char *data, size_t length,
    Ini_Options options);

/// Parses an ini file from a file pointer.
Ini_Parse_Result ini_parse_file (FILE *fp, Ini_Options options);

/// Gets a reference to a table, if nesting was enabled during parsing the
/// name is interpreted as a nested path using the specified delimiter.
const Ini_Table * ini_get_table (const Ini *self, const char *name);

/// Gets a property from a table.
Ini_String ini_table_get (const Ini_Table *self, const char *name);

/// Gets a nested table from a table
const Ini_Table * ini_table_get_table (const Ini_Table *self, const char *name);

/// Gets a value from the given table, if nesting was enabled during parsing
/// the table name is interpreted as a nested path using the specified
/// delimiter.
Ini_String ini_get (const Ini *self, const char *table, const char *name);

/// Destroys the ini object.
void ini_free (Ini *self);

#endif /* INI_H */
