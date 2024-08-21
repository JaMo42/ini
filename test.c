#define RBT_IMPLEMENTATION
#include <assert.h>
#include <string.h>

// Include source to get access to internal functions
#include "ini.c"

extern int ini_compare_string(const char *a, const char *b, size_t len);

int scmp(Ini_String a, const char *b) {
    // This is the only way it is currently used.
    return ini_compare_string(a.data, b, a.size);
}

static inline void assert_value (Ini_String value, const char *expected)
{
  assert (!!value.data == !!expected);
  if (value.data) {
    assert (strcmp (value.data, expected) == 0);
  }
}

static inline void assert_error (Ini_Parse_Result result, const char *error, unsigned line)
{
  assert (!result.ok);
  assert (strcmp (result.error, error) == 0);
  assert (result.error_line == line);
}

void test_internals() {
#define S(x) ((Ini_String) { (char*)(x), sizeof(x)-1 })
#define sign(x) (((x) > 0) - ((x) < 0))
#define compat(a, b) (sign(scmp(S(a), (b))) == sign(strcmp((a), (b))))
    assert(scmp(S("name"), "name1") != 0);
    assert(scmp(S("name1"), "name") != 0);
    assert(scmp(S("a"), "ab") < 0);
    assert(scmp(S("ab"), "a") > 0);
    assert(compat("a", "ab"));
    assert(compat("ab", "a"));
    assert(compat("foo", "bar"));
    assert(compat("bar", "foo"));
    assert(compat("baz", "bar"));
    assert(compat("bar", "baz"));
#undef compat
#undef sign
#undef S
    puts("Success: test_internals");
}

void test_stable ()
{
  FILE *f = fopen ("test_stable.ini", "r");
  Ini_Parse_Result result = ini_parse_file (f, ini_options_stable);
  fclose (f);
  assert (result.ok);
  Ini *ini = &result.unwrap;
  const Ini_Table *namespace1 = ini_get_table (ini, "namespace1");
  const Ini_Table *section = ini_get_table (ini, "section");
  assert_value (ini_table_get (namespace1, "name"), "value");
  assert_value (ini_table_get (namespace1, "unicode"), "안녕하세요");
  assert_value (ini_table_get (section, "key1"), "a");
  assert_value (ini_table_get (section, "key2"), "b");
  assert_value (ini_get (ini, "foo", "bar"), "baz ; this is not a comment");
  assert_value (ini_get (ini, "section", "c"), NULL);
  assert_value (ini_get (ini, "foo", "empty_value"), "");
  assert_value (ini_get (ini, "foo", "sAmE"), "xyz");
  puts ("Success: test_stable");
  ini_free (ini);
}

void test_all ()
{
  FILE *f = fopen ("test_all.ini", "r");
  Ini_Options options = ini_options_stable;
  options.flags = INI_ALL_FLAGS;
  Ini_Parse_Result result = ini_parse_file (f, options);
  fclose (f);
  assert (result.ok);
  Ini *ini = &result.unwrap;
  assert_value (ini_get (ini, "a.b.c", "foo"), "bar");
  assert_value (ini_get (ini, "", "global1"), "hello");
  assert_value (ini_get (ini, "", "global2"), "world");
  assert_value (ini_get (ini, "special", "special-value"), "hello\tworld");
  assert_value (ini_get (ini, "a", "test"), "test;test");
  assert_value (ini_get (ini, "a", "empty"), "");
  {
    Ini_String with_null = ini_get (ini, "special", "with-null");
    assert (with_null.data);
    const char expected[] = "hello\0world";
    for (size_t i = 0; i < sizeof (expected); ++i) {
      assert (with_null.data[i] == expected[i]);
    }
  }
  assert_value (ini_get (ini, "special", "unicode"), "\U00012345 \u0123");
  puts ("Success: test_all");
  ini_free(ini);
}

void test_errors ()
{
  const char *unclosed_section = "[section\nname=value";
  const char *no_value = "[section]\nname\n";
  const char *unallowed_global = "name=value\n";
  const char *unicode_too_large = "u='\\U00110000'";
  const char *unicode_high_surrogate = "u='\\uD820'";
  const char *unicode_low_surrogate = "u='\\uDC20'";
  const char *unicode_4_missing = "u='\\u123'";
  const char *unicode_8_missing = "u='\\U12345'";
  const Ini_Options all_options = INI_OPTIONS_WITH_FLAGS (INI_ALL_FLAGS);
  assert_error (
    ini_parse_string (unclosed_section, 0, ini_options_stable),
    "unclosed section", 1
  );
  assert_error (
    ini_parse_string (no_value, 0, ini_options_stable),
    "name without value", 2
  );
  assert_error (
    ini_parse_string (unallowed_global, 0, ini_options_stable),
    "no table defined", 1
  );
  assert_error (
    ini_parse_string (unicode_too_large, 0, all_options),
    "illegal Unicode character", 1
  );
  assert_error (
    ini_parse_string (unicode_low_surrogate, 0, all_options),
    "illegal Unicode character", 1
  );
  assert_error (
    ini_parse_string (unicode_high_surrogate, 0, all_options),
    "illegal Unicode character", 1
  );
  assert_error (
    ini_parse_string (unicode_4_missing, 0, all_options),
    "truncated \\uXXXX escape", 1
  );
  assert_error (
    ini_parse_string (unicode_8_missing, 0, all_options),
    "truncated \\UXXXXXXXX escape", 1
  );
  puts ("Success: test_errors");
}

int main ()
{
  test_internals();
  test_stable ();
  test_all ();
  test_errors ();
}

