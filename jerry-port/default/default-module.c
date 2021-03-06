/* Copyright JS Foundation and other contributors, http://js.foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if !defined (_WIN32)
#include <unistd.h>
#else
#include <direct.h>
#include <Windows.h>
#endif /* !defined (_WIN32) */
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "jerryscript-port.h"
#include "jerryscript-port-default.h"
#include "cwalk.h"

#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

/**
 * Convert utf16 string to utf8 string
 * @return the utf8 string length
 */
size_t
jerry_convert_utf16_to_utf8 (const uint16_t *in_wide_str_p,
                             size_t in_wide_str_size,
                             char *out_str_p,
                             size_t out_str_size)
{
#if defined (_WIN32)
  size_t utf8_len = (size_t) WideCharToMultiByte (
      CP_UTF8, 0, (wchar_t *) in_wide_str_p, (int) in_wide_str_size,
      NULL, 0, NULL, NULL);

  if (out_str_p != NULL && utf8_len < out_str_size)
  {
    WideCharToMultiByte (
        CP_UTF8, 0, (wchar_t *) in_wide_str_p, (int) in_wide_str_size,
        out_str_p, (int) utf8_len, 0, 0);
    out_str_p[utf8_len] = 0;
  }
  return utf8_len;
#else
  (void) (in_wide_str_p);
  (void) (in_wide_str_size);
  (void) (out_str_p);
  (void) (out_str_size);
  return 0;
#endif
} /* jerry_convert_utf16_to_utf8 */

/**
 * Get the current working directory
 *
 * @return the working directory path
 */
char *
jerry_port_get_cwd (char *out_buf_p,     /**< output buffer */
                    size_t out_buf_size) /**< size of output buffer */
{
  char *out_cwd_p = NULL;
#if defined (_WIN32)
  wchar_t *cwd_wide_p = _wgetcwd (NULL, 0);
  if (cwd_wide_p != NULL)
  {
    if (out_buf_p != NULL)
    {
      /* Try copy into out_buf_p, if the length are not enough,
        then setting out_cwd_p to NULL */
      size_t utf8_len = jerry_convert_utf16_to_utf8 (
          cwd_wide_p, wcslen (cwd_wide_p), out_buf_p, out_buf_size);
      if (utf8_len < out_buf_size)
      {
        out_cwd_p = out_buf_p;
      }
    }
    else
    {
      /* Try allocating out_cwd_p, if failed, then setting out_cwd_p to NULL */
      size_t utf8_len = jerry_convert_utf16_to_utf8 (
          cwd_wide_p, wcslen (cwd_wide_p), NULL, 0);
      out_cwd_p = malloc (utf8_len + 1);
      jerry_convert_utf16_to_utf8 (
          cwd_wide_p, wcslen (cwd_wide_p), out_cwd_p, utf8_len + 1);
    }
    /* cwd_wide_p will free in any condition */
    free (cwd_wide_p);
  }
#else
  char *cwd_p = getcwd (NULL, 0);
  if (cwd_p != NULL)
  {
    if (out_buf_p != NULL)
    {
      size_t cwd_p_len = strlen (cwd_p);
      if (cwd_p_len < out_buf_size)
      {
        memcpy (out_buf_p, cwd_p, cwd_p_len);
        out_buf_p[cwd_p_len] = 0;
        out_cwd_p = out_buf_p;
      }
      /* Free cwd_p only when out_buf_p are not NULL */
      free (cwd_p);
    }
    else
    {
      out_cwd_p = cwd_p;
    }
  }
#endif
  if (out_cwd_p == NULL && out_buf_p == NULL)
  {
    out_cwd_p = malloc (2);
    if (out_cwd_p != NULL)
    {
      out_cwd_p[0] = '/';
      out_cwd_p[1] = 0;
    }
  }
  return out_cwd_p;
} /* jerry_port_get_cwd  */

/**
 * Determines the size of the given file.
 * @return size of the file
 */
static size_t
jerry_port_get_file_size (FILE *file_p) /**< opened file */
{
  fseek (file_p, 0, SEEK_END);
  long size = ftell (file_p);
  fseek (file_p, 0, SEEK_SET);

  return (size_t) size;
} /* jerry_port_get_file_size */

/**
 * Opens file with the given path and reads its source.
 * @return the source of the file
 */
uint8_t *
jerry_port_read_source (const char *file_name_p, /**< file name */
                        size_t *out_size_p) /**< [out] read bytes */
{
  struct stat stat_buffer;
  if (stat (file_name_p, &stat_buffer) == -1 || S_ISDIR (stat_buffer.st_mode))
  {
    jerry_port_log (JERRY_LOG_LEVEL_ERROR, "Error: Failed to open file: %s\n", file_name_p);
    return NULL;
  }

  FILE *file_p = fopen (file_name_p, "rb");

  if (file_p == NULL)
  {
    jerry_port_log (JERRY_LOG_LEVEL_ERROR, "Error: Failed to open file: %s\n", file_name_p);
    return NULL;
  }

  size_t file_size = jerry_port_get_file_size (file_p);
  uint8_t *buffer_p = (uint8_t *) malloc (file_size);

  if (buffer_p == NULL)
  {
    fclose (file_p);

    jerry_port_log (JERRY_LOG_LEVEL_ERROR, "Error: Failed to allocate memory for file: %s\n", file_name_p);
    return NULL;
  }

  size_t bytes_read = fread (buffer_p, 1u, file_size, file_p);

  if (bytes_read != file_size)
  {
    fclose (file_p);
    free (buffer_p);

    jerry_port_log (JERRY_LOG_LEVEL_ERROR, "Error: Failed to read file: %s\n", file_name_p);
    return NULL;
  }

  fclose (file_p);
  *out_size_p = bytes_read;

  return buffer_p;
} /* jerry_port_read_source */

/**
 * Release the previously opened file's content.
 */
void
jerry_port_release_source (uint8_t *buffer_p) /**< buffer to free */
{
  free (buffer_p);
} /* jerry_port_release_source */

/**
 * Computes the end of the directory part of a path.
 *
 * @return end of the directory part of a path.
 */
static size_t
jerry_port_get_directory_end (const jerry_char_t *path_p) /**< path */
{
  const jerry_char_t *end_p = path_p + strlen ((const char *) path_p);

  while (end_p > path_p)
  {
#if defined (_WIN32)
    if (end_p[-1] == '/' || end_p[-1] == '\\')
    {
      return (size_t) (end_p - path_p);
    }
#else /* !_WIN32 */
    if (end_p[-1] == '/')
    {
      return (size_t) (end_p - path_p);
    }
#endif /* _WIN32 */

    end_p--;
  }

  return 0;
} /* jerry_port_get_directory_end */

/**
 * Normalize a file path.
 *
 * @return a newly allocated buffer with the normalized path if the operation is successful,
 *         NULL otherwise
 */
jerry_char_t *
jerry_port_normalize_path (const jerry_char_t *in_path_p, /**< path to the referenced module */
                           size_t in_path_length, /**< length of the path */
                           const jerry_char_t *base_path_p, /**< base path */
                           size_t base_path_length) /**< length of the base path */
{
  char *path_p;
  char *base_path_pz;
  char *in_path_pz = malloc (in_path_length + 1);
  if (in_path_pz == NULL)
  {
    return NULL;
  }
  memcpy (in_path_pz, in_path_p, in_path_length);
  in_path_pz[in_path_length] = '\0';

  if (base_path_length > 0)
  {
    base_path_pz = malloc (base_path_length + 1);
    if (base_path_pz != NULL)
    {
      memcpy (base_path_pz, base_path_p, base_path_length);
      base_path_pz[base_path_length] = '\0';
    }
  }
  else
  {
    base_path_pz = jerry_port_get_cwd (NULL, 0);
  }

  if (base_path_pz == NULL)
  {
    free (in_path_pz);
    return NULL;
  }

  size_t path_p_len = cwk_path_get_absolute (base_path_pz, in_path_pz, NULL, 0);
  path_p = (char *) malloc (path_p_len + 1);
  if (path_p != NULL)
  {
    cwk_path_get_absolute (base_path_pz, in_path_pz, path_p, path_p_len + 1);
    path_p[path_p_len] = '\0';
  }
  free (base_path_pz);
  free (in_path_pz);

  return (jerry_char_t *) path_p;
} /* jerry_port_normalize_path */

/**
 * A module descriptor.
 */
typedef struct jerry_port_module_t
{
  struct jerry_port_module_t *next_p; /**< next_module */
  jerry_char_t *path_p; /**< path to the module */
  size_t base_path_length; /**< base path length for relative difference */
  jerry_value_t realm; /**< the realm of the module */
  jerry_value_t module; /**< the module itself */
} jerry_port_module_t;

/**
 * Native info descriptor for modules.
 */
static const jerry_object_native_info_t jerry_port_module_native_info =
{
  .free_cb = NULL,
};

/**
 * Default module manager.
 */
typedef struct
{
  jerry_port_module_t *module_head_p; /**< first module */
} jerry_port_module_manager_t;

/**
 * Release known modules.
 */
static void
jerry_port_module_free (jerry_port_module_manager_t *manager_p, /**< module manager */
                        const jerry_value_t realm) /**< if this argument is object, release only those modules,
                                                    *   which realm value is equal to this argument. */
{
  jerry_port_module_t *module_p = manager_p->module_head_p;

  bool release_all = !jerry_value_is_object (realm);

  jerry_port_module_t *prev_p = NULL;

  while (module_p != NULL)
  {
    jerry_port_module_t *next_p = module_p->next_p;

    if (release_all || module_p->realm == realm)
    {
      free (module_p->path_p);
      jerry_release_value (module_p->realm);
      jerry_release_value (module_p->module);

      free (module_p);

      if (prev_p == NULL)
      {
        manager_p->module_head_p = next_p;
      }
      else
      {
        prev_p->next_p = next_p;
      }
    }
    else
    {
      prev_p = module_p;
    }

    module_p = next_p;
  }
} /* jerry_port_module_free */

/**
 * Initialize the default module manager.
 */
static void
jerry_port_module_manager_init (void *user_data_p)
{
  ((jerry_port_module_manager_t *) user_data_p)->module_head_p = NULL;
} /* jerry_port_module_manager_init */

/**
 * Deinitialize the default module manager.
 */
static void
jerry_port_module_manager_deinit (void *user_data_p) /**< context pointer to deinitialize */
{
  jerry_value_t undef = jerry_create_undefined ();
  jerry_port_module_free ((jerry_port_module_manager_t *) user_data_p, undef);
  jerry_release_value (undef);
} /* jerry_port_module_manager_deinit */

/**
 * Declare the context data manager for modules.
 */
static const jerry_context_data_manager_t jerry_port_module_manager =
{
  .init_cb = jerry_port_module_manager_init,
  .deinit_cb = jerry_port_module_manager_deinit,
  .bytes_needed = sizeof (jerry_port_module_manager_t)
};

/**
 * Default module resolver.
 *
 * @return a module object if resolving is successful, an error otherwise
 */
jerry_value_t
jerry_port_module_resolve (const jerry_value_t specifier, /**< module specifier string */
                           const jerry_value_t referrer, /**< parent module */
                           void *user_p) /**< user data */
{
  (void) user_p;

  jerry_port_module_t *module_p;
  const jerry_char_t *base_path_p = NULL;
  size_t base_path_length = 0;

  if (jerry_get_object_native_pointer (referrer, (void **) &module_p, &jerry_port_module_native_info))
  {
    base_path_p = module_p->path_p;
    base_path_length = module_p->base_path_length;
  }

  jerry_size_t in_path_length = jerry_get_utf8_string_size (specifier);
  jerry_char_t *in_path_p = (jerry_char_t *) malloc (in_path_length + 1);
  jerry_string_to_utf8_char_buffer (specifier, in_path_p, in_path_length);
  in_path_p[in_path_length] = '\0';

  jerry_char_t *path_p = jerry_port_normalize_path (in_path_p, in_path_length, base_path_p, base_path_length);

  if (path_p == NULL)
  {
    return jerry_create_error (JERRY_ERROR_COMMON, (const jerry_char_t *) "Out of memory");
  }

  jerry_value_t realm = jerry_get_global_object ();

  jerry_port_module_manager_t *manager_p;
  manager_p = (jerry_port_module_manager_t *) jerry_get_context_data (&jerry_port_module_manager);

  module_p = manager_p->module_head_p;

  while (module_p != NULL)
  {
    if (module_p->realm == realm
        && strcmp ((const char *) module_p->path_p, (const char *) path_p) == 0)
    {
      free (path_p);
      free (in_path_p);
      jerry_release_value (realm);
      return jerry_acquire_value (module_p->module);
    }

    module_p = module_p->next_p;
  }

  size_t source_size;
  uint8_t *source_p = jerry_port_read_source ((const char *) path_p, &source_size);

  if (source_p == NULL)
  {
    free (path_p);
    free (in_path_p);
    jerry_release_value (realm);
    /* TODO: This is incorrect, but makes test262 module tests pass
     * (they should throw SyntaxError, but not because the module cannot be found). */
    return jerry_create_error (JERRY_ERROR_SYNTAX, (const jerry_char_t *) "Module file not found");
  }

  jerry_parse_options_t parse_options;
  parse_options.options = JERRY_PARSE_MODULE | JERRY_PARSE_HAS_RESOURCE;
  parse_options.resource_name_p = (jerry_char_t *) in_path_p;
  parse_options.resource_name_length = (size_t) in_path_length;

  jerry_value_t ret_value = jerry_parse (source_p,
                                         source_size,
                                         &parse_options);

  jerry_port_release_source (source_p);
  free (in_path_p);

  if (jerry_value_is_error (ret_value))
  {
    free (path_p);
    jerry_release_value (realm);
    return ret_value;
  }

  module_p = (jerry_port_module_t *) malloc (sizeof (jerry_port_module_t));

  module_p->next_p = manager_p->module_head_p;
  module_p->path_p = path_p;
  module_p->base_path_length = jerry_port_get_directory_end (module_p->path_p);
  module_p->realm = realm;
  module_p->module = jerry_acquire_value (ret_value);

  jerry_set_object_native_pointer (ret_value, module_p, &jerry_port_module_native_info);
  manager_p->module_head_p = module_p;

  return ret_value;
} /* jerry_port_module_resolve */

/**
 * Release known modules.
 */
void
jerry_port_module_release (const jerry_value_t realm) /**< if this argument is object, release only those modules,
                                                       *   which realm value is equal to this argument. */
{
  jerry_port_module_free ((jerry_port_module_manager_t *) jerry_get_context_data (&jerry_port_module_manager),
                          realm);
} /* jerry_port_module_release */
