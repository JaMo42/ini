# ini

read-only C/C++ ini parser.

Note: throughout this library sections are usually referred to as tables.

## Dependencies

- [rb_tree](https://github.com/JaMo42/rb-tree)

## Supported format

Most features described on [Wikipedia](https://en.wikipedia.org/wiki/INI_file) are implemented.

Which [varying features](https://en.wikipedia.org/wiki/INI_file#Varying_features) to use can the selected when parsing:

- Global properties (`INI_GLOBAL_PROPS` flag)

- Hierarchy (section nesting) (`INI_NESTING` flag)

- Inline comments (`INI_INLINE_COMMENTS` flag)

- Quoted values (`INI_QUOTED_VALUES` flag)

  - These are values that are surrounded by `"` or `'`.
    Inside of these the following escape sequences can be used for special characters:

    - `\\` \ (a single backslash, escaping the escape character)

    - `\'` Apostrophe (also if the string uses double quotes)

    - `\"` Double quotes (also if the string uses apostrophes)

    - `\0` Null character

    - `\a` Bell/Alert/Audible

    - `\t` Tab character

    - `\r` Carriage return

    - `\n` Newline

    - `\x??` A single byte value, there are currently no Unicode escapes

Some special characters can also be changed:

- The name-value delimiter (uses `=` in the stable options)

- The comment character (uses `;` in the stable options)

- The path delimiter for nested sections (this set to `.` in the stable options,
  however it not used in the stable options and is only set for copying them)

The structure to define these options is declared as:

```
typedef struct {
  unsigned char flags;
  char name_value_delim;
  char comment_char;
  char section_delim;
} Ini_Options;
```

Predefined values:

```
extern const Ini_Options ini_options_stable;

#define INI_OPTIONS_WITH_FLAGS(flags_)
```

The `ini_options_stable` value defines only the [stable features](https://en.wikipedia.org/wiki/INI_file#Stable_features).

The `INI_OPTIONS_WITH_FLAGS` macro creates a configuration with custom flags, copying the special characters from  ini_stable_options`.

## Parsing

Files can be parsed from either a string or a file pointer:

```
Ini_Parse_Result ini_parse_string (const char *data, size_t length, Ini_Options options);

Ini_Parse_Result ini_parse_file (FILE *fp, Ini_Options options);
```

If the `length` arguments is `0` the string is parsed until a null terminator.

The `Ini_Parse_Result` structure is declared as:

```
typedef struct ini_parse_result {
  Ini unwrap;
  const char *error;
  bool ok;
} Ini_Parse_Result;
```

- `unwrap` hold the parsed object

- `error` contains a error description if the was an error during parsing (or `Success` if there was none)

- `ok` whether parsing was successful or an error occurred.

If an error occurrs the partially parsed ini object is free'd before returning the result.

Note: All strings inside the ini object are allocated so the file/string that was parsed can be discarded after calling these functions.

## Getting tables

```
const Ini_Table * ini_get_table (const Ini *self, const char *name);

const Ini_Table * ini_table_get_table (const Ini_Table *self, const char *name);
```

The `ini_get_table` gets a table using it's absolute path.
If `INI_NESTING` was enabled during parsing the given path uses the same path delimiter as specified in the options.
If no matching table is found `NULL` is returned.

The `ini_table_get_table` gets a nested table from a table.
If `INI_NESTING` was not enabled during parsing this function always returns `NULL`.

## Getting properties

```
typedef struct {
  char *data;
  size_t size;
} Ini_String;
```

This structure represents the value of a string.
The `data` field is a null-terminated string holding the value and `size` the length of that string.

Note that if `INI_QUOTED_VALUES` was used the string may also contain additional null bytes.

```
Ini_String ini_table_get (const Ini_Table *self, const char *name);

Ini_String ini_get (const Ini *self, const char *table, const char *name);
```

`ini_table_get` reads a property from a table, if a property with the given name is not found the `data` of the returned string is `NULL` and the `size` is `0`.

The `ini_get` functions is equivalent to a `ini_get_table` followed by a `ini_table_get`, it returns `NULL` if either the table of the value is not found.

## Other

```
void ini_Free (Ini *self);
```

Destroys the ini object.
