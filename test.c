#include "ini.h"
#include <assert.h>
#include <string.h>

static inline void assert_value (Ini_String value, const char *expected)
{
  assert (!!value.data == !!expected);
  if (value.data) {
    assert (strcmp (value.data, expected) == 0);
  }
}

static inline void assert_error (Ini_Parse_Result result, const char *error)
{
  assert (!result.ok);
  assert (strcmp (result.error, error) == 0);
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
  ini_free (ini);
  puts ("Success: test_all");
}

void test_errors ()
{
  const char *unclosed_section = "[section\nname=value";
  const char *no_value = "[section]\nname\n";
  const char *unallowed_global = "name=value\n";
  assert_error (
    ini_parse_string (unclosed_section, 0, ini_options_stable),
    "unclosed section"
  );
  assert_error (
    ini_parse_string (no_value, 0, ini_options_stable),
    "name without value"
  );
  assert_error (
    ini_parse_string (unallowed_global, 0, ini_options_stable),
    "no table defined"
  );
  puts ("Success: test_errors");
}

int main ()
{
  test_stable ();
  test_all ();
  test_errors ();
}

