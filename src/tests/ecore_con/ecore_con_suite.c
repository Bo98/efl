#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ecore_con_suite.h"
#include "../efl_check.h"
#include <Ecore.h>
#include <Ecore_Con.h>

static const Efl_Test_Case etc[] = {
  { "Ecore_Con", ecore_con_test_ecore_con },
  { "Ecore_Con_Url", ecore_con_test_ecore_con_url },
  { "Ecore_Con_Eet", ecore_con_test_ecore_con_eet },
  { "Efl_Net_Ip_Address", ecore_con_test_efl_net_ip_address },
  { NULL, NULL }
};

#define TIMEOUT 10.0
static Ecore_Timer *timeout_timer;

static Eina_Bool
_timeout_timer(void *d EINA_UNUSED)
{
   timeout_timer = NULL;
   ck_abort_msg("Timeout exceeded!");
   return EINA_FALSE;
}

SUITE_INIT(ecore_con)
{
   ck_assert_int_eq(ecore_init(), 1);
   ck_assert_int_eq(ecore_con_init(), 1);
   timeout_timer = ecore_timer_add(TIMEOUT, _timeout_timer, NULL);
}

SUITE_SHUTDOWN(ecore_con)
{
   if (timeout_timer) ecore_timer_del(timeout_timer);
   timeout_timer = NULL;
   ck_assert_int_eq(ecore_con_shutdown(), 0);
   ck_assert_int_eq(ecore_shutdown(), 0);
}

int
main(int argc, char **argv)
{
   int failed_count;

   if (!_efl_test_option_disp(argc, argv, etc))
     return 0;

#ifdef NEED_RUN_IN_TREE
   putenv("EFL_RUN_IN_TREE=1");
#endif

   failed_count = _efl_suite_build_and_run(argc - 1, (const char **)argv + 1,
                                           "Ecore_Con", etc, SUITE_INIT_FN(ecore_con), SUITE_SHUTDOWN_FN(ecore_con));

   return (failed_count == 0) ? 0 : 255;
}
