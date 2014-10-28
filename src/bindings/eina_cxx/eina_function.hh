#ifndef EFL_EINA_FUNCTION_HH
#define EFL_EINA_FUNCTION_HH

namespace efl { namespace eina { namespace _mpl {

template <typename T>
struct function_params;

template <typename R, typename... P>
struct function_params<R(*)(P...)>
{
  typedef std::tuple<P...> type;
};
      
} } }

#endif
