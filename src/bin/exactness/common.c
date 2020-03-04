#include "common.h"

static int _verbose = 0;

void
ex_printf(int verbose, const char *fmt, ...)
{
   va_list ap;
   if (!_verbose || verbose > _verbose) return;

   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
}

Eina_Bool
ex_mkdir(const char *path, Eina_Bool skip_last)
{
   if (!ecore_file_exists(path))
     {
        const char *cur = path + 1;
        do
          {
             char *slash = strchr(cur, '/');
             if (slash) *slash = '\0';
             else if (skip_last) return EINA_TRUE;
             if (!ecore_file_exists(path) && !ecore_file_mkdir(path)) return EINA_FALSE;
             if (slash) *slash = '/';
             if (slash) cur = slash + 1;
             else cur = NULL;
          }
        while (cur);
     }
   return EINA_TRUE;
}
