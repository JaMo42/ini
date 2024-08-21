#include "ini.h"
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>

#define INI_STRING_NONE ((Ini_String) { NULL, 0 })

#define INI_MAX(a, b) (((a) > (b)) ? (a) : (b))

const Ini_Options ini_options_stable = {
  .flags = 0,
  .name_value_delim = '=',
  .comment_char = ';',
  // The section delimiter is ignored for these options as nesting is not set
  // in the flags. It is set to the default anyways to allow the user to copy
  // this object and only change the flags.
  .section_delim = '.'
};

typedef char (*ini_next_byte_t) (void **, const void *);

typedef struct {
  /// Configuration
  const Ini_Options options;
  /// Information for reading strings or files
  void *stream;
  const void *end;
  ini_next_byte_t next_byte;
  /// The ini object
  Ini the;
  /// The table values get inserted into
  Ini_Table *current_table;
  /// The error message (`NULL` if no error)
  const char *error;
} Ini_Parse_Context;

typedef struct {
  char *data;
  size_t capacity;
  size_t size;
} Ini_Array;

typedef struct {
  struct rbt_node rbt_node;
  char *key;
  union {
    Ini_String as_string;
    Ini_Table as_table;
  };
} Ini_Node;

#define INI_NODE(n) RBT_CONTAINER_OF((n), Ini_Node, rbt_node)

static Ini_Parse_Context ini_create_context (Ini_Options options)
{
  return (Ini_Parse_Context) {
    .options = options,
    .stream = NULL,
    .end = NULL,
    .next_byte = NULL,
    .the = (Ini) {
      .tables_and_globals = (Ini_Table) {
        .values = RBT_EMPTY,
        .tables = RBT_EMPTY,
      },
      .options = options,
    },
    .current_table = NULL,
    .error = NULL,
  };
}


static char ini_next_byte_string (void **stream_in, const void *end)
{
  char **stream = (char **)stream_in;
  if (*stream_in == end)
    return EOF;
  return *(*stream)++;
}


static char ini_next_byte_file (void **stream_in, const void *end)
{
  (void)end;
  FILE *stream = *(FILE **)stream_in;
  return fgetc (stream);
}


static int ini_compare_string (const char *a, const char *b, size_t len)
{
  int A, B;
  while (*a && *b && len--) {
    A = toupper (*a++);
    B = toupper (*b++);
    if (A != B) {
      return A - B;
    }
  }
  // Note: assuming `a` is an `Ini_String` that may not be null terminated,
  // but `b` is always null terminated, which is currently the case.
  bool a_is_done = len == 0;
  bool b_is_done = *b == 0;
  return b_is_done - a_is_done;
}


static inline size_t ini_string_find (Ini_String s, char ch)
{
  char *const p = (char *)memchr (s.data, ch, s.size);
  return p ? (size_t)(p - s.data) : (size_t)-1;
}


static Ini_Node * ini_set_node (struct rbtree *tree, Ini_String key)
{
  struct rbt_node *node = tree->root, *parent = NULL;
  enum rbt_direction dir = RBT_LEFT;
  while (node) {
    Ini_Node *const data = INI_NODE (node);
    const int cmp = ini_compare_string (key.data, data->key, key.size);
    parent = node;
    if (cmp < 0) {
      node = node->left;
      dir = RBT_LEFT;
    } else if (cmp > 0) {
      node = node->right;
      dir = RBT_RIGHT;
    } else {
      return data;
    }
  }
  Ini_Node *const new_node = (Ini_Node *)malloc (sizeof (Ini_Node));
  new_node->key = (char *)malloc (key.size + 1);
  memcpy (new_node->key, key.data, key.size);
  new_node->key[key.size] = '\0';
  memset (&new_node->as_string, 0, INI_MAX (sizeof (Ini_String), sizeof (Ini_Table)));
  rbt_insert (tree, &new_node->rbt_node, parent, dir);
  return new_node;
}


static Ini_Node * ini_process_nested (struct rbtree *tables,
    Ini_String full_name, char delim,
    Ini_Node * (*f) (struct rbtree *, Ini_String))
{
    Ini_Node *result = NULL;
    size_t i;
    while ((i = ini_string_find (full_name, delim)) != (size_t)-1) {
      const Ini_String key = { full_name.data, i };
      full_name.data += i + 1;
      full_name.size -= i + 1;
      result = f (tables, key);
      if (result == NULL) {
        return NULL;
      }
      tables = &result->as_table.tables;
    }
    result = f (tables, full_name);
    if (result == NULL) {
      return NULL;
    }
    tables = &result->as_table.tables;
    return result;
}


static Ini_Node * ini_set_nested (Ini_Parse_Context *pc, Ini_String full_name)
{
  const char delim = pc->options.section_delim;
  if (full_name.data[0] == delim) {
    ++full_name.data;
    --full_name.size;
    if (pc->current_table == &pc->the.tables_and_globals) {
      return ini_set_node (&pc->the.tables_and_globals.tables, full_name);
    } else {
      return ini_set_node (&pc->current_table->tables, full_name);
    }
  }
  return ini_process_nested (
    &pc->the.tables_and_globals.tables, full_name, delim, ini_set_node
  );
}


static Ini_Node * ini_get_node (struct rbtree *tree, Ini_String key)
{
  struct rbt_node *node = tree->root;
  while (node) {
    Ini_Node *const data = INI_NODE (node);
    const int cmp = ini_compare_string (key.data, data->key, key.size);
    if (cmp < 0) {
      node = node->left;
    } else if (cmp > 0) {
      node = node->right;
    } else {
      return data;
    }
  }
  return NULL;
}


static Ini_Node * ini_get_nested (struct rbtree *tables, Ini_String full_name,
    char delim)
{
  return ini_process_nested (tables, full_name, delim, ini_get_node);
}


static bool ini_get_line (Ini_Parse_Context *pc, Ini_Array *line)
{
  bool is_eof = false;
  char ch;
  line->size = 0;
  while ((ch = pc->next_byte (&pc->stream, pc->end)) != '\n') {
    if (ch == EOF) {
      is_eof = true;
      break;
    }
    if (line->size == line->capacity) {
      line->capacity *= 2;
      line->data = (char *)realloc (line->data, line->capacity + 1);
    }
    line->data[line->size++] = ch;
  }
  // Remove CR in case it uses DOS line endings
  if (line->size && line->data[line->size - 1] == 0x0D) {
    --line->size;
  }
  line->data[line->size] = '\0';
  return is_eof;
}


static inline bool ini_isspace (char ch)
{
  return ch == ' ' || ch == '\t';
}


static void ini_strip (Ini_String *line)
{
  size_t i;
  char *const data = line->data;
  i = 0;
  while (i < line->size && ini_isspace (data[i])) {
    ++i;
  }
  memmove (data, data + i, line->size - i);
  line->size -= i;
  if (line->size == 0) {
    line->data[0] = '\0';
    return;
  }
  i = line->size - 1;
  while (i > 0 && ini_isspace (data[i])) {
    --i;
  }
  line->data[i+1] = '\0';
  line->size = i + 1;
}


static void ini_parse_section (Ini_Parse_Context *pc, Ini_String line)
{
  if (line.data[line.size - 1] != ']') {
    pc->error = "unclosed section";
    return;
  } else if (line.size == 2) {
    if (pc->options.flags & INI_GLOBAL_PROPS) {
      pc->current_table = &pc->the.tables_and_globals;
    } else {
      pc->error = "global scopes not allowed";
    }
    return;
  }
  Ini_String name = { line.data + 1, line.size - 2 };
  name.data[name.size] = '\0';
  Ini_Table *table = NULL;
  if (pc->options.flags & INI_NESTING) {
    table = &ini_set_nested (pc, name)->as_table;
  } else {
    table = &ini_set_node (&pc->the.tables_and_globals.tables, name)->as_table;
  }
  pc->current_table = table;
}


static inline int ini_unicode_escape (Ini_Parse_Context *pc, char *out_,
    const char **source)
{
  unsigned char *out = (unsigned char *)out_;
  const int digits = **source == 'u' ? 4 : 8;
  uint32_t codepoint = 0;
  char ch;
  for (int i = 0; i < digits; ++i) {
    ch = *++(*source);
    if (!isxdigit (ch)) {
      if (digits == 4) {
        pc->error = "truncated \\uXXXX escape";
      } else {
        pc->error = "truncated \\UXXXXXXXX escape";
      }
      return 0;
    }
    codepoint *= 16;
    switch (ch) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        codepoint += ch - '0';
        break;

      case 'a':
      case 'b':
      case 'c':
      case 'd':
      case 'e':
      case 'f':
        codepoint += 10 + ch - 'a';
        break;

      case 'A':
      case 'B':
      case 'C':
      case 'D':
      case 'E':
      case 'F':
        codepoint += 10 + ch - 'A';
        break;
    }
  }
  // Note: surrogates are considered illegal characters since the text is
  //       always stored as utf-8.
      // Greater than highest unicode character
  if (codepoint > 0x10FFFF
      // High surrogates
      || (codepoint >= 0xD800 && codepoint <= 0xDBFF)
      // Low surrogates
      || (codepoint >= 0xDC00 && codepoint <= 0xDFFF)) {
    pc->error = "illegal Unicode character";
    return 0;
  }
  if (codepoint < (1 << 7)) {
    *out = codepoint;
    return 1;
  }
  if (codepoint < (1 << 11)) {
    *out++ = 0xC0 | (codepoint >> 6);
    *out   = 0x80 | (codepoint & 0x3F);
    return 2;
  }
  if (codepoint < (1 << 16)) {
    *out++ = 0xE0 | (codepoint >> 12);
    *out++ = 0x80 | ((codepoint >> 6) & 0x3F);
    *out   = 0x80 | (codepoint & 0x3F);
    return 3;
  }
  if (1) {
    *out++ = 0xF0 | (codepoint >> 18);
    *out++ = 0x80 | ((codepoint >> 12) & 0x3F);
    *out++ = 0x80 | ((codepoint >> 6) & 0x3F);
    *out   = 0x80 | (codepoint & 0x3F);
    return 4;
  }
}


static inline const char * ini_process_quoted (Ini_Parse_Context *pc,
    Ini_String *out, Ini_String quoted)
{
  const char *p = quoted.data;
  const char *const end = p + quoted.size;
  const char quote_char = *p++;
  // The result will at most be the same length as the quoted string.
  // We subtract 2 from the size for the 2 quoting characters and add 1 back
  // for the null terminator
  if (out->data) {
    out->data = (char *)realloc (out->data, quoted.size - 1);
  } else {
    out->data = (char *)malloc (quoted.size - 1);
  }
  char *write = out->data;
  size_t size = 0;
  int code;
  for (; p != end; ++p) {
    if (*p == '\\') {
      ++p;
      switch (*p) {
      case '\\':
        *write++ = '\\';
        break;

      case '\'':
        *write++ = '\'';
        break;

      case '"':
        *write++ = '"';
        break;

      case '0':
        *write++ = '\0';
        break;

      case 'a':
        *write++ = '\a';
        break;

      case 't':
        *write++ = '\t';
        break;

      case 'r':
        *write++ = '\r';
        break;

      case 'n':
        *write++ = '\n';
        break;

      case 'x':
        code = *++p * 16;
        code += *++p;
        *write++ = code;
        break;

      case 'u':
      case 'U':
        code = ini_unicode_escape (pc, write, &p);
        if (pc->error) {
          return NULL;
        }
        write += code;
        size += code - 1;
        break;

      default:
        // Ignore unknown escapes
        break;
      }
    } else if (*p == quote_char) {
      out->data[size] = '\0';
      out->size = size;
      return ++p;
    } else {
      *write++ = *p;
    }
    ++size;
  }
  return NULL;
}


static inline void ini_set_value (Ini_Parse_Context *pc,
    Ini_String *out, Ini_String raw, const Ini_Options *options)
{
  const bool inline_comments = (options->flags & INI_INLINE_COMMENTS) != 0;
  if ((raw.data[0] == '\'' || raw.data[0] == '"')
      && (options->flags & INI_QUOTED_VALUES) != 0) {
    const char *const end = ini_process_quoted (pc, out, raw);
    if (pc->error) {
      return;
    }
    if (end == NULL) {
      pc->error = "unterminated quoted value";
      return;
    }
    if (*end != '\0' && !inline_comments) {
      pc->error = "trailing characters after quoted string";
      return;
    }
    ini_strip (out);
    return;
  }
  size_t comment = 0;
  if ((options->flags & INI_INLINE_COMMENTS) != 0) {
    for (comment = 0; comment < raw.size; ++comment) {
      if (raw.data[comment] == options->comment_char) {
        if (comment == 0) {
          out->data = (char *)malloc (1);
          out->data[0] = '\0';
          out->size = 0;
          return;
        }
        if (ini_isspace (raw.data[comment-1])) {
          break;
        }
      }
    }
  }
  out->size = comment ? comment : raw.size;
  if (out->data) {
    out->data = (char *)realloc (out->data, out->size + 1);
  } else {
    out->data = (char *)malloc (out->size + 1);
  }
  memcpy (out->data, raw.data, out->size);
  out->data[out->size] = '\0';
  ini_strip (out);
  return;
}


static void ini_parse_key_value (Ini_Parse_Context *pc, Ini_String line)
{
  char *const peq = strchr (line.data, pc->options.name_value_delim);
  if (peq == NULL) {
    pc->error = "name without value";
    return;
  } else if (pc->current_table == NULL) {
    pc->error = "no table defined";
    return;
  }
  const size_t eq = peq - line.data;
  Ini_String name = { line.data, eq };
  ini_strip (&name);

  Ini_String raw_value = {peq + 1, line.size - eq - 1};
  ini_strip (&raw_value);

  Ini_Node *node = ini_set_node (&pc->current_table->values, name);

  ini_set_value (pc, &node->as_string, raw_value, &pc->options);
}


static void ini_parse_line (Ini_Parse_Context *pc, Ini_String line)
{
  if (line.data[0] == '\0' || line.data[0] == pc->options.comment_char) {
    return;
  } else if (line.data[0] == '[') {
    ini_parse_section (pc, line);
  } else {
    ini_parse_key_value (pc, line);
  }
}


static Ini_Parse_Result ini_parse (Ini_Parse_Context *pc)
{
  Ini_Array linebuf = {
    .data = (char *)malloc (256+1),
    .capacity= 256,
    .size = 0
  };
  Ini_String line;
  if (pc->options.flags & INI_GLOBAL_PROPS) {
    pc->current_table = &pc->the.tables_and_globals;
  }
  unsigned line_number = 0;
  for (;;) {
    ++line_number;
    const bool is_eof = ini_get_line (pc, &linebuf);
    line.data = linebuf.data;
    line.size = linebuf.size;
    ini_strip (&line);
    ini_parse_line (pc, line);
    if (pc->error) {
      ini_free (&pc->the);
      free (linebuf.data);
      return (Ini_Parse_Result) {
        .unwrap = pc->the,
        .error = pc->error,
        .error_line = line_number,
        .ok = false
      };
    }
    if (is_eof) {
      break;
    }
  }
  free (linebuf.data);
  return (Ini_Parse_Result) {
    .unwrap = pc->the,
    .error = "Success",
    .error_line = 0,
    .ok = true
  };
}


Ini_Parse_Result ini_parse_string (const char *data, size_t length,
    Ini_Options options)
{
  if (length == 0) {
    length = strlen (data);
  }
  Ini_Parse_Context pc = ini_create_context (options);
  pc.stream = (void *)data;
  pc.end = data + length;
  pc.next_byte = ini_next_byte_string;
  return ini_parse (&pc);
}


Ini_Parse_Result ini_parse_file (FILE *fp, Ini_Options options)
{
  Ini_Parse_Context pc = ini_create_context (options);
  pc.stream = fp;
  pc.end = NULL;
  pc.next_byte = ini_next_byte_file;
  return ini_parse (&pc);
}


const Ini_Table * ini_get_table (const Ini *self, const char *name)
{
  if (*name == '\0') {
    if (self->options.flags & INI_GLOBAL_PROPS) {
      return &self->tables_and_globals;
    } else {
      return NULL;
    }
  }
  Ini_String sname = { (char *)name, strlen (name) };
  struct rbtree *tables = (struct rbtree *)&self->tables_and_globals.tables;
  Ini_Node *node;
  if (self->options.flags & INI_NESTING) {
    node = ini_get_nested (tables, sname, self->options.section_delim);
  } else {
    node = ini_get_node (tables, sname);
  }
  return node ? &node->as_table : NULL;
}


Ini_String ini_table_get (const Ini_Table *self, const char *name)
{
  if (*name == '\0') {
    return INI_STRING_NONE;
  }
  Ini_String sname = { (char *)name, strlen (name) };
  Ini_Node *node = ini_get_node ((struct rbtree *)&self->values, sname);
  return node ? node->as_string : INI_STRING_NONE;
}


const Ini_Table * ini_table_get_table (const Ini_Table *self, const char *name)
{
  if (*name == '\0') {
    return NULL;
  }
  Ini_String sname = { (char *)name, strlen (name) };
  Ini_Node *node = ini_get_node ((struct rbtree *)&self->tables, sname);
  return node ? &node->as_table : NULL;
}


Ini_String ini_get (const Ini *self, const char *table, const char *name)
{
  const Ini_Table *the_table = ini_get_table (self, table);
  if (!the_table) {
    return INI_STRING_NONE;
  }
  return ini_table_get (the_table, name);
}


static void ini_free_values (struct rbtree *values)
{
  if (values->root == NULL) {
    return;
  }
  struct rbt_node *it, *next = rbt_first (values);
  struct rbt_node *const last = rbt_last (values);
  Ini_Node *node = NULL;
  do {
    it = next;
    next = rbt_next (it);
    free (node);
    node = INI_NODE (it);
    free (node->key);
    free (node->as_string.data);
  } while (it != last);
  free (node);
}


static void ini_free_table (Ini_Table *table)
{
  ini_free_values (&table->values);
  if (table->tables.root == NULL) {
    return;
  }
  struct rbt_node *it, *next = rbt_first (&table->tables);
  struct rbt_node *const last = rbt_last (&table->tables);
  Ini_Node *node = NULL;
  do {
    it = next;
    next = rbt_next (it);
    free (node);
    node = INI_NODE (it);
    free (node->key);
    ini_free_table (&node->as_table);
  } while (it != last);
  free (node);
}


void ini_free (Ini *self)
{
  ini_free_table (&self->tables_and_globals);
}
