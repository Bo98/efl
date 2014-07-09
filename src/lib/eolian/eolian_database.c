#include <Eina.h>
#include "eo_parser.h"
#include "eolian_database.h"

#define PROP_GET_RETURN_DFLT_VAL "property_get_return_dflt_val"
#define PROP_SET_RETURN_DFLT_VAL "property_set_return_dflt_val"
#define METHOD_RETURN_DFLT_VAL "method_return_dflt_val"

#define EOLIAN_METHOD_RETURN_TYPE "method_return_type"
#define EOLIAN_PROP_GET_RETURN_TYPE "property_get_return_type"
#define EOLIAN_PROP_SET_RETURN_TYPE "property_set_return_type"

#define EOLIAN_METHOD_RETURN_COMMENT "method_return_comment"
#define EOLIAN_PROP_GET_RETURN_COMMENT "property_get_return_comment"
#define EOLIAN_PROP_SET_RETURN_COMMENT "property_set_return_comment"

static Eina_List *_classes = NULL;
static Eina_Hash *_types = NULL;
static Eina_Hash *_structs = NULL;
static Eina_Hash *_filenames = NULL; /* Hash: filename without extension -> full path */
static Eina_Hash *_tfilenames = NULL;
static int _database_init_count = 0;

struct _Eolian_Class
{
   Eina_Stringshare *full_name;
   Eina_List *namespaces; /* List Eina_Stringshare * */
   Eina_Stringshare *name;
   Eina_Stringshare *file;
   Eolian_Class_Type type;
   Eina_Stringshare *description;
   Eina_Stringshare *legacy_prefix;
   Eina_Stringshare *eo_prefix;
   Eina_Stringshare *data_type;
   Eina_List *inherits; /* List Eina_Stringshare * */
   Eina_List *properties; /* List prop_name -> Eolian_Function */
   Eina_List *methods; /* List meth_name -> Eolian_Function */
   Eina_List *constructors; /* List constructor_name -> Eolian_Function */
   Eina_List *implements; /* List implements name -> Eolian_Implement */
   Eina_List *events; /* List event_name -> Eolian_Event */
   Eina_Bool class_ctor_enable:1;
   Eina_Bool class_dtor_enable:1;
};

typedef struct
{
   Eina_Stringshare *alias;
   Eolian_Type *type;
} Type_Desc;

struct _Eolian_Function
{
   Eina_Stringshare *name;
   Eina_List *keys; /* list of Eolian_Function_Parameter */
   Eina_List *params; /* list of Eolian_Function_Parameter */
   Eolian_Function_Type type;
   Eolian_Function_Scope scope;
   Eolian_Type *get_ret_type;
   Eolian_Type *set_ret_type;
   Eina_Hash *data;
   Eina_Bool obj_is_const :1; /* True if the object has to be const. Useful for a few methods. */
   Eina_Bool get_virtual_pure :1;
   Eina_Bool set_virtual_pure :1;
   Eina_Bool get_return_warn_unused :1; /* also used for methods */
   Eina_Bool set_return_warn_unused :1;
};

struct _Eolian_Function_Parameter
{
   Eina_Stringshare *name;
   Eolian_Type *type;
   Eina_Stringshare *description;
   Eolian_Parameter_Dir param_dir;
   Eina_Bool is_const_on_get :1; /* True if const in this the get property */
   Eina_Bool is_const_on_set :1; /* True if const in this the set property */
   Eina_Bool nonull :1; /* True if this argument cannot be NULL */
};

/* maps directly to Eo_Type_Def */

typedef struct
{
   Eolian_Type *type;
   const char *comment;
} _Struct_Field_Type;

struct _Eolian_Type
{
   const char        *name;
   Eolian_Type_Type   type;
   union {
      struct {
         Eina_List   *subtypes;
         Eolian_Type *base_type;
      };
      struct {
         Eina_List   *arguments;
         Eolian_Type *ret_type;
      };
      struct {
         Eina_Hash  *fields;
         const char *comment;
      };
   };
   Eina_Bool is_const  :1;
   Eina_Bool is_own    :1;
};

struct _Eolian_Implement
{
   Eina_Stringshare *full_name;
};

struct _Eolian_Event
{
   Eina_Stringshare *name;
   Eina_Stringshare *type;
   Eina_Stringshare *comment;
};

static void
_param_del(Eolian_Function_Parameter *pdesc)
{
   eina_stringshare_del(pdesc->name);

   database_type_del(pdesc->type);
   eina_stringshare_del(pdesc->description);
   free(pdesc);
}

void
database_type_del(Eolian_Type *type)
{
   if (!type) return;
   eo_definitions_type_free((Eo_Type_Def*)type);
}

static void
_fid_del(Eolian_Function *fid)
{
   Eolian_Function_Parameter *param;
   if (!fid) return;
   eina_stringshare_del(fid->name);
   eina_hash_free(fid->data);
   EINA_LIST_FREE(fid->keys, param) _param_del(param);
   EINA_LIST_FREE(fid->params, param) _param_del(param);
   database_type_del(fid->get_ret_type);
   database_type_del(fid->set_ret_type);
   free(fid);
}

static void
_class_del(Eolian_Class *class)
{
   Eina_Stringshare *inherit_name;
   Eina_List *inherits = class->inherits;
   EINA_LIST_FREE(inherits, inherit_name)
      eina_stringshare_del(inherit_name);

   Eolian_Implement *impl;
   Eina_List *implements = class->implements;
   EINA_LIST_FREE(implements, impl)
     {
        eina_stringshare_del(impl->full_name);
        free(impl);
     }

   Eolian_Function *fid;
   Eolian_Event *ev;
   EINA_LIST_FREE(class->constructors, fid) _fid_del(fid);
   EINA_LIST_FREE(class->methods, fid) _fid_del(fid);
   EINA_LIST_FREE(class->properties, fid) _fid_del(fid);
   EINA_LIST_FREE(class->events, ev) database_event_free(ev);

   eina_stringshare_del(class->name);
   eina_stringshare_del(class->full_name);
   eina_stringshare_del(class->file);
   eina_stringshare_del(class->description);
   eina_stringshare_del(class->legacy_prefix);
   eina_stringshare_del(class->eo_prefix);
   eina_stringshare_del(class->data_type);
   free(class);
}

static void _type_hash_free_cb(void *data)
{
   Type_Desc *type = data;
   eina_stringshare_del(type->alias);
   database_type_del(type->type);
   free(type);
}

int
database_init()
{
   if (_database_init_count > 0) return ++_database_init_count;
   eina_init();
   _types = eina_hash_stringshared_new(_type_hash_free_cb);
   _structs = eina_hash_stringshared_new(EINA_FREE_CB(database_type_del));
   _filenames = eina_hash_string_small_new(free);
   _tfilenames = eina_hash_string_small_new(free);
   return ++_database_init_count;
}

int
database_shutdown()
{
   if (_database_init_count <= 0)
     {
        ERR("Init count not greater than 0 in shutdown.");
        return 0;
     }
   _database_init_count--;

   if (_database_init_count == 0)
     {
        Eolian_Class *class;
        EINA_LIST_FREE(_classes, class)
           _class_del(class);
        eina_hash_free(_types);
        eina_hash_free(_structs);
        eina_hash_free(_filenames);
        eina_hash_free(_tfilenames);
        eina_shutdown();
     }
   return _database_init_count;
}

Eina_Bool
database_type_add(const char *alias, Eolian_Type *type)
{
   if (_types)
     {
        Type_Desc *desc = calloc(1, sizeof(*desc));
        desc->alias = eina_stringshare_add(alias);
        desc->type = type;
        eina_hash_set(_types, desc->alias, desc);
        return EINA_TRUE;
     }
   return EINA_FALSE;
}

Eina_Bool database_struct_add(Eolian_Type *tp)
{
   if (_structs)
     {
        eina_hash_set(_structs, tp->name, tp);
        return EINA_TRUE;
     }
   return EINA_FALSE;
}

EAPI Eolian_Type *
eolian_type_find_by_alias(const char *alias)
{
   if (!_types) return NULL;
   Eina_Stringshare *shr = eina_stringshare_add(alias);
   Type_Desc *cl = eina_hash_find(_types, shr);
   eina_stringshare_del(shr);
   return cl?cl->type:NULL;
}

EAPI Eolian_Type *
eolian_type_struct_find_by_name(const char *name)
{
   if (!_structs) return NULL;
   Eina_Stringshare *shr = eina_stringshare_add(name);
   Eolian_Type *tp = eina_hash_find(_structs, shr);
   eina_stringshare_del(shr);
   return tp;
}

Eolian_Class *
database_class_add(const char *class_name, Eolian_Class_Type type)
{
   char *full_name = strdup(class_name);
   char *name = full_name;
   char *colon = full_name;
   Eolian_Class *cl = calloc(1, sizeof(*cl));
   cl->full_name = eina_stringshare_add(class_name);
   cl->type = type;
   do
     {
        colon = strchr(colon, '.');
        if (colon)
          {
             *colon = '\0';
             cl->namespaces = eina_list_append(cl->namespaces, eina_stringshare_add(name));
             colon += 1;
             name = colon;
          }
     }
   while(colon);
   cl->name = eina_stringshare_add(name);
   _classes = eina_list_append(_classes, cl);
   free(full_name);
   return cl;
}

Eina_Bool
database_class_file_set(Eolian_Class *class, const char *file_name)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(class, EINA_FALSE);
   class->file = eina_stringshare_add(file_name);
   return EINA_TRUE;
}

EAPI const char *
eolian_class_file_get(const Eolian_Class *cl)
{
   return cl ? cl->file : NULL;
}

EAPI const char *
eolian_class_full_name_get(const Eolian_Class *cl)
{
   return cl ? cl->full_name : NULL;
}

EAPI const char *
eolian_class_name_get(const Eolian_Class *cl)
{
   return cl ? cl->name : NULL;
}

EAPI const Eina_List *
eolian_class_namespaces_list_get(const Eolian_Class *cl)
{
   return cl ? cl->namespaces : NULL;
}

EAPI Eolian_Class *
eolian_class_find_by_name(const char *class_name)
{
   Eina_List *itr;
   Eolian_Class *cl;
   Eina_Stringshare *shr_name = eina_stringshare_add(class_name);
   EINA_LIST_FOREACH(_classes, itr, cl)
      if (cl->full_name == shr_name) goto end;
   cl = NULL;
end:
   eina_stringshare_del(shr_name);
   return cl;
}

/*
 * ret false -> clash, class = NULL
 * ret true && class -> only one class corresponding
 * ret true && !class -> no class corresponding
 */
Eina_Bool database_class_name_validate(const char *class_name, Eolian_Class **class)
{
   char *name = strdup(class_name);
   char *colon = name + 1;
   Eolian_Class *found_class = NULL;
   Eolian_Class *candidate;
   if (class) *class = NULL;
   do
     {
        colon = strchr(colon, '.');
        if (colon) *colon = '\0';
        candidate = eolian_class_find_by_name(name);
        if (candidate)
          {
             if (found_class)
               {
                  ERR("Name clash between class %s and class %s",
                        candidate->full_name,
                        found_class->full_name);
                  free(name);
                  return EINA_FALSE; // Names clash
               }
             found_class = candidate;
          }
        if (colon) *colon++ = '.';
     }
   while(colon);
   if (class) *class = found_class;
   free(name);
   return EINA_TRUE;
}

EAPI Eolian_Class *
eolian_class_find_by_file(const char *file_name)
{
   Eina_List *itr;
   Eolian_Class *cl;
   Eina_Stringshare *shr_file = eina_stringshare_add(file_name);
   EINA_LIST_FOREACH(_classes, itr, cl)
      if (cl->file == shr_file) goto end;
   cl = NULL;
end:
   eina_stringshare_del(shr_file);
   return cl;
}

EAPI Eolian_Class_Type
eolian_class_type_get(const Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, EOLIAN_CLASS_UNKNOWN_TYPE);
   return cl->type;
}

Eina_Bool
database_class_del(Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, EINA_FALSE);
   _classes = eina_list_remove(_classes, cl);
   _class_del(cl);
   return EINA_TRUE;
}

EAPI const Eina_List *
eolian_all_classes_list_get(void)
{
   return _classes;
}

Eina_Bool
database_class_inherit_add(Eolian_Class *cl, const char *inherit_class_name)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, EINA_FALSE);
   cl->inherits = eina_list_append(cl->inherits, eina_stringshare_add(inherit_class_name));
   return EINA_TRUE;
}

EAPI const char *
eolian_class_description_get(const Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, NULL);
   return cl->description;
}

void
database_class_description_set(Eolian_Class *cl, const char *description)
{
   EINA_SAFETY_ON_NULL_RETURN(cl);
   cl->description = eina_stringshare_add(description);
}

EAPI const char*
eolian_class_legacy_prefix_get(const Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, NULL);
   return cl->legacy_prefix;
}

void
database_class_legacy_prefix_set(Eolian_Class *cl, const char *legacy_prefix)
{
   EINA_SAFETY_ON_NULL_RETURN(cl);
   cl->legacy_prefix = eina_stringshare_add(legacy_prefix);
}

EAPI const char*
eolian_class_eo_prefix_get(const Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, NULL);
   return cl->eo_prefix;
}

void
database_class_eo_prefix_set(Eolian_Class *cl, const char *eo_prefix)
{
   EINA_SAFETY_ON_NULL_RETURN(cl);
   cl->eo_prefix = eina_stringshare_add(eo_prefix);
}

EAPI const char*
eolian_class_data_type_get(const Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, NULL);
   return cl->data_type;
}

void
database_class_data_type_set(Eolian_Class *cl, const char *data_type)
{
   EINA_SAFETY_ON_NULL_RETURN(cl);
   cl->data_type = eina_stringshare_add(data_type);
}

EAPI const Eina_List *
eolian_class_inherits_list_get(const Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, NULL);
   //FIXME: create list here
   return cl->inherits;
}

EAPI const Eina_List*
eolian_class_implements_list_get(const Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, NULL);
   return cl->implements;
}

Eolian_Function *
database_function_new(const char *function_name, Eolian_Function_Type foo_type)
{
   Eolian_Function *fid = calloc(1, sizeof(*fid));
   fid->name = eina_stringshare_add(function_name);
   fid->type = foo_type;
   fid->data = eina_hash_string_superfast_new(free);
   return fid;
}

EAPI Eolian_Function_Scope
eolian_function_scope_get(const Eolian_Function *fid)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, EOLIAN_SCOPE_PUBLIC);
   return fid->scope;
}

void
database_function_scope_set(Eolian_Function *fid, Eolian_Function_Scope scope)
{
   EINA_SAFETY_ON_NULL_RETURN(fid);
   fid->scope = scope;
}

void
database_function_type_set(Eolian_Function *fid, Eolian_Function_Type foo_type)
{
   EINA_SAFETY_ON_NULL_RETURN(fid);
   switch (foo_type)
     {
      case EOLIAN_PROP_SET:
         if (fid->type == EOLIAN_PROP_GET) foo_type = EOLIAN_PROPERTY;
         break;
      case EOLIAN_PROP_GET:
         if (fid->type == EOLIAN_PROP_SET) foo_type = EOLIAN_PROPERTY;
         break;
      default:
         break;
     }
   fid->type = foo_type;
}

Eina_Bool database_class_function_add(Eolian_Class *cl, Eolian_Function *fid)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(fid && cl, EINA_FALSE);
   switch (fid->type)
     {
      case EOLIAN_PROPERTY:
      case EOLIAN_PROP_SET:
      case EOLIAN_PROP_GET:
         cl->properties = eina_list_append(cl->properties, fid);
         break;
      case EOLIAN_METHOD:
         cl->methods = eina_list_append(cl->methods, fid);
         break;
      case EOLIAN_CTOR:
         cl->constructors = eina_list_append(cl->constructors, fid);
         break;
      default:
         ERR("Bad function type %d.", fid->type);
         return EINA_FALSE;
     }
   return EINA_TRUE;
}

Eolian_Implement *
database_implement_new(const char *impl_name)
{
   Eolian_Implement *impl_desc = calloc(1, sizeof(Eolian_Implement));
   EINA_SAFETY_ON_NULL_RETURN_VAL(impl_desc, NULL);
   impl_desc->full_name = eina_stringshare_add(impl_name);
   return impl_desc;
}

Eina_Bool
database_class_implement_add(Eolian_Class *cl, Eolian_Implement *impl_desc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(impl_desc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, EINA_FALSE);
   cl->implements = eina_list_append(cl->implements, impl_desc);
   return EINA_TRUE;
}

EAPI Eina_Stringshare *
eolian_implement_full_name_get(const Eolian_Implement *impl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(impl, NULL);
   return impl->full_name;
}

EAPI Eina_Bool
eolian_implement_information_get(const Eolian_Implement *impl, Eolian_Class **class_out, Eolian_Function **func_out, Eolian_Function_Type *type_out)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(impl, EINA_FALSE);
   Eolian_Class *class;
   if (!database_class_name_validate(impl->full_name, &class) || !class) return EINA_FALSE;
   const char *class_name = class->full_name;
   if (class_out) *class_out = class;

   char *func_name = strdup(impl->full_name + strlen(class_name) + 1);
   char *colon = strchr(func_name, '.');
   Eolian_Function_Type type = EOLIAN_UNRESOLVED;
   if (colon)
     {
        *colon = '\0';
        if (!strcmp(colon+1, "set")) type = EOLIAN_PROP_SET;
        else if (!strcmp(colon+1, "get")) type = EOLIAN_PROP_GET;
     }

   Eolian_Function *fid = eolian_class_function_find_by_name(class, func_name, type);
   if (func_out) *func_out = fid;
   if (type == EOLIAN_UNRESOLVED) type = eolian_function_type_get(fid);
   if (type_out) *type_out = type;
   free(func_name);
   return EINA_TRUE;
}

EAPI Eolian_Function *
eolian_class_function_find_by_name(const Eolian_Class *cl, const char *func_name, Eolian_Function_Type f_type)
{
   Eina_List *itr;
   Eolian_Function *fid;
   if (!cl) return NULL;

   if (f_type == EOLIAN_UNRESOLVED || f_type == EOLIAN_METHOD)
      EINA_LIST_FOREACH(cl->methods, itr, fid)
        {
           if (!strcmp(fid->name, func_name))
              return fid;
        }

   if (f_type == EOLIAN_UNRESOLVED || f_type == EOLIAN_PROPERTY ||
         f_type == EOLIAN_PROP_SET || f_type == EOLIAN_PROP_GET)
     {
        EINA_LIST_FOREACH(cl->properties, itr, fid)
          {
             if (!strcmp(fid->name, func_name))
                return fid;
          }
     }

   if (f_type == EOLIAN_UNRESOLVED || f_type == EOLIAN_CTOR)
     {
        EINA_LIST_FOREACH(cl->constructors, itr, fid)
          {
             if (!strcmp(fid->name, func_name))
                return fid;
          }
     }

   ERR("Function %s not found in class %s", func_name, cl->name);
   return NULL;
}

EAPI const Eina_List *
eolian_class_functions_list_get(const Eolian_Class *cl, Eolian_Function_Type foo_type)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, NULL);
   switch (foo_type)
     {
      case EOLIAN_PROPERTY:
         return cl->properties;
      case EOLIAN_METHOD:
         return cl->methods;
      case EOLIAN_CTOR:
         return cl->constructors;
      default: return NULL;
     }
}

EAPI Eolian_Function_Type
eolian_function_type_get(const Eolian_Function *fid)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, EOLIAN_UNRESOLVED);
   return fid->type;
}

EAPI const char *
eolian_function_name_get(const Eolian_Function *fid)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, NULL);
   return fid->name;
}

EAPI const char *
eolian_function_full_c_name_get(const Eolian_Function *foo_id, const char *prefix)
{
   const char  *funcn = eolian_function_name_get(foo_id);
   const char  *last_p = strrchr(prefix, '_');
   const char  *func_p = strchr(funcn, '_');
   Eina_Strbuf *buf = eina_strbuf_new();
   Eina_Stringshare *ret;
   int   len;

   if (!last_p) last_p = prefix;
   else last_p++;
   if (!func_p) len = strlen(funcn);
   else len = func_p - funcn;

   if ((int)strlen(last_p) != len || strncmp(last_p, funcn, len))
     {
        eina_strbuf_append(buf, prefix);
        eina_strbuf_append_char(buf, '_');
        eina_strbuf_append(buf, funcn);
        ret = eina_stringshare_add(eina_strbuf_string_get(buf));
        eina_strbuf_free(buf);
        return ret;
     }

   if (last_p != prefix)
      eina_strbuf_append_n(buf, prefix, last_p - prefix); /* includes _ */

   eina_strbuf_append(buf, funcn);
   ret = eina_stringshare_add(eina_strbuf_string_get(buf));
   eina_strbuf_free(buf);
   return ret;
}

Eina_Bool
database_function_set_as_virtual_pure(Eolian_Function *fid, Eolian_Function_Type ftype)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, EINA_FALSE);
   switch (ftype)
     {
      case EOLIAN_UNRESOLVED: case EOLIAN_METHOD: case EOLIAN_PROP_GET: fid->get_virtual_pure = EINA_TRUE; break;
      case EOLIAN_PROP_SET: fid->set_virtual_pure = EINA_TRUE; break;
      default: return EINA_FALSE;
     }
   return EINA_TRUE;
}

EAPI Eina_Bool
eolian_function_is_virtual_pure(const Eolian_Function *fid, Eolian_Function_Type ftype)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, EINA_FALSE);
   switch (ftype)
     {
      case EOLIAN_UNRESOLVED: case EOLIAN_METHOD: case EOLIAN_PROP_GET: return fid->get_virtual_pure; break;
      case EOLIAN_PROP_SET: return fid->set_virtual_pure; break;
      default: return EINA_FALSE;
     }
}

void
database_function_data_set(Eolian_Function *fid, const char *key, const char *data)
{
   EINA_SAFETY_ON_NULL_RETURN(key);
   EINA_SAFETY_ON_NULL_RETURN(fid);
   if (data)
     {
        if (!eina_hash_find(fid->data, key))
          eina_hash_set(fid->data, key, strdup(data));
     }
   else
     {
        eina_hash_del(fid->data, key, NULL);
     }
}

EAPI const char *
eolian_function_data_get(const Eolian_Function *fid, const char *key)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, NULL);
   return eina_hash_find(fid->data, key);
}

static Eolian_Function_Parameter *
_parameter_new(Eolian_Type *type, const char *name, const char *description)
{
   Eolian_Function_Parameter *param = NULL;
   param = calloc(1, sizeof(*param));
   param->name = eina_stringshare_add(name);
   param->type = type;
   param->description = eina_stringshare_add(description);
   return param;
}

Eolian_Function_Parameter *
database_property_key_add(Eolian_Function *fid, Eolian_Type *type, const char *name, const char *description)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, NULL);
   Eolian_Function_Parameter *param = _parameter_new(type, name, description);
   fid->keys = eina_list_append(fid->keys, param);
   return param;
}

Eolian_Function_Parameter *
database_property_value_add(Eolian_Function *fid, Eolian_Type *type, const char *name, const char *description)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, NULL);
   Eolian_Function_Parameter *param = _parameter_new(type, name, description);
   fid->params = eina_list_append(fid->params, param);
   return param;
}

Eolian_Function_Parameter *
database_method_parameter_add(Eolian_Function *fid, Eolian_Parameter_Dir param_dir, Eolian_Type *type, const char *name, const char *description)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, NULL);
   Eolian_Function_Parameter *param = _parameter_new(type, name, description);
   param->param_dir = param_dir;
   fid->params = eina_list_append(fid->params, param);
   return param;
}

EAPI Eolian_Function_Parameter *
eolian_function_parameter_get(const Eolian_Function *fid, const char *param_name)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, NULL);
   Eina_List *itr;
   Eolian_Function_Parameter *param;
   EINA_LIST_FOREACH(fid->keys, itr, param)
      if (!strcmp(param->name, param_name)) return param;
   EINA_LIST_FOREACH(fid->params, itr, param)
      if (!strcmp(param->name, param_name)) return param;
   return NULL;
}

EAPI Eolian_Type *
eolian_parameter_type_get(const Eolian_Function_Parameter *param)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(param, NULL);
   return param->type;
}

EAPI Eina_Stringshare *
eolian_parameter_name_get(const Eolian_Function_Parameter *param)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(param, NULL);
   eina_stringshare_ref(param->name);
   return param->name;
}

EAPI const Eina_List *
eolian_property_keys_list_get(const Eolian_Function *fid)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, NULL);
   return fid->keys;
}

EAPI const Eina_List *
eolian_property_values_list_get(const Eolian_Function *fid)
{
   return eolian_parameters_list_get(fid);
}

EAPI const Eina_List *
eolian_parameters_list_get(const Eolian_Function *fid)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, NULL);
   return fid->params;
}

/* Get parameter information */
EAPI void
eolian_parameter_information_get(const Eolian_Function_Parameter *param, Eolian_Parameter_Dir *param_dir, Eolian_Type **type, const char **name, const char **description)
{
   EINA_SAFETY_ON_NULL_RETURN(param);
   if (param_dir) *param_dir = param->param_dir;
   if (type) *type = param->type;
   if (name) *name = param->name;
   if (description) *description = param->description;
}

void
database_parameter_const_attribute_set(Eolian_Function_Parameter *param, Eina_Bool is_get, Eina_Bool is_const)
{
   EINA_SAFETY_ON_NULL_RETURN(param);
   if (is_get)
      param->is_const_on_get = is_const;
   else
      param->is_const_on_set = is_const;
}

void
database_parameter_type_set(Eolian_Function_Parameter *param, Eolian_Type *types)
{
   EINA_SAFETY_ON_NULL_RETURN(param);
   param->type = types;
}

EAPI Eina_Bool
eolian_parameter_const_attribute_get(const Eolian_Function_Parameter *param, Eina_Bool is_get)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(param, EINA_FALSE);
   if (is_get)
      return param->is_const_on_get;
   else
      return param->is_const_on_set;
}

void
database_parameter_nonull_set(Eolian_Function_Parameter *param, Eina_Bool nonull)
{
   EINA_SAFETY_ON_NULL_RETURN(param);
   param->nonull = nonull;
}

EAPI Eina_Bool
eolian_parameter_is_nonull(const Eolian_Function_Parameter *param)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(param, EINA_FALSE);
   return param->nonull;
}

void database_function_return_type_set(Eolian_Function *fid, Eolian_Function_Type ftype, Eolian_Type *ret_type)
{
   switch (ftype)
     {
      case EOLIAN_PROP_SET: fid->set_ret_type = ret_type; break;
      case EOLIAN_UNRESOLVED: case EOLIAN_METHOD: case EOLIAN_PROP_GET: fid->get_ret_type = ret_type; break;
      default: return;
     }
}

EAPI Eolian_Type *
eolian_function_return_type_get(const Eolian_Function *fid, Eolian_Function_Type ftype)
{
   switch (ftype)
     {
      case EOLIAN_PROP_SET: return fid->set_ret_type;
      case EOLIAN_UNRESOLVED: case EOLIAN_METHOD: case EOLIAN_PROP_GET: return fid->get_ret_type;
      default: return NULL;
     }
}

void database_function_return_dflt_val_set(Eolian_Function *fid, Eolian_Function_Type ftype, const char *ret_dflt_value)
{
   const char *key = NULL;
   switch (ftype)
     {
      case EOLIAN_PROP_SET: key = PROP_SET_RETURN_DFLT_VAL; break;
      case EOLIAN_PROP_GET: key = PROP_GET_RETURN_DFLT_VAL; break;
      case EOLIAN_METHOD: key = METHOD_RETURN_DFLT_VAL; break;
      default: return;
     }
   database_function_data_set(fid, key, ret_dflt_value);
}

EAPI const char *
eolian_function_return_dflt_value_get(const Eolian_Function *fid, Eolian_Function_Type ftype)
{
   const char *key = NULL;
   switch (ftype)
     {
      case EOLIAN_PROP_SET: key = PROP_SET_RETURN_DFLT_VAL; break;
      case EOLIAN_PROP_GET: key = PROP_GET_RETURN_DFLT_VAL; break;
      case EOLIAN_UNRESOLVED: case EOLIAN_METHOD: key = METHOD_RETURN_DFLT_VAL; break;
      default: return NULL;
     }
   return eolian_function_data_get(fid, key);
}

EAPI const char *
eolian_function_return_comment_get(const Eolian_Function *fid, Eolian_Function_Type ftype)
{
   const char *key = NULL;
   switch (ftype)
     {
      case EOLIAN_PROP_SET: key = EOLIAN_PROP_SET_RETURN_COMMENT; break;
      case EOLIAN_PROP_GET: key = EOLIAN_PROP_GET_RETURN_COMMENT; break;
      case EOLIAN_UNRESOLVED: case EOLIAN_METHOD: key = EOLIAN_METHOD_RETURN_COMMENT; break;
      default: return NULL;
     }
   return eolian_function_data_get(fid, key);
}

void database_function_return_comment_set(Eolian_Function *fid, Eolian_Function_Type ftype, const char *ret_comment)
{
   const char *key = NULL;
   switch (ftype)
     {
      case EOLIAN_PROP_SET: key = EOLIAN_PROP_SET_RETURN_COMMENT; break;
      case EOLIAN_PROP_GET: key = EOLIAN_PROP_GET_RETURN_COMMENT; break;
      case EOLIAN_METHOD: key = EOLIAN_METHOD_RETURN_COMMENT; break;
      default: return;
     }
   database_function_data_set(fid, key, ret_comment);
}

void database_function_return_flag_set_as_warn_unused(Eolian_Function *fid,
      Eolian_Function_Type ftype, Eina_Bool warn_unused)
{
   EINA_SAFETY_ON_NULL_RETURN(fid);
   switch (ftype)
     {
      case EOLIAN_METHOD: case EOLIAN_PROP_GET: fid->get_return_warn_unused = warn_unused; break;
      case EOLIAN_PROP_SET: fid->set_return_warn_unused = warn_unused; break;
      default: return;
     }
}

EAPI Eina_Bool
eolian_function_return_is_warn_unused(const Eolian_Function *fid,
      Eolian_Function_Type ftype)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, EINA_FALSE);
   switch (ftype)
     {
      case EOLIAN_METHOD: case EOLIAN_PROP_GET: return fid->get_return_warn_unused;
      case EOLIAN_PROP_SET: return fid->set_return_warn_unused;
      default: return EINA_FALSE;
     }
}

void
database_function_object_set_as_const(Eolian_Function *fid, Eina_Bool is_const)
{
   EINA_SAFETY_ON_NULL_RETURN(fid);
   fid->obj_is_const = is_const;
}

EAPI Eina_Bool
eolian_function_object_is_const(const Eolian_Function *fid)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, EINA_FALSE);
   return fid->obj_is_const;
}

Eolian_Event *
database_event_new(const char *event_name, const char *event_type, const char *event_comment)
{
   if (!event_name) return NULL;
   Eolian_Event *event_desc = calloc(1, sizeof(Eolian_Event));
   if (!event_desc) return NULL;
   event_desc->name = eina_stringshare_add(event_name);
   if (event_type) event_desc->type = eina_stringshare_add(event_type);
   event_desc->comment = eina_stringshare_add(event_comment);
   return event_desc;
}

void
database_event_free(Eolian_Event *event)
{
   eina_stringshare_del(event->name);
   eina_stringshare_del(event->comment);
   free(event);
}

Eina_Bool
database_class_event_add(Eolian_Class *cl, Eolian_Event *event_desc)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL(event_desc && cl, EINA_FALSE);
   cl->events = eina_list_append(cl->events, event_desc);
   return EINA_TRUE;
}

EAPI const Eina_List*
eolian_class_events_list_get(const Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, NULL);
   return cl->events;
}

EAPI Eina_Bool
eolian_class_event_information_get(const Eolian_Event *event, const char **event_name, const char **event_type, const char **event_comment)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(event, EINA_FALSE);
   if (event_name) *event_name = event->name;
   if (event_type) *event_type = event->type;
   if (event_comment) *event_comment = event->comment;
   return EINA_TRUE;
}

Eina_Bool
database_class_ctor_enable_set(Eolian_Class *cl, Eina_Bool enable)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, EINA_FALSE);
   cl->class_ctor_enable = enable;
   return EINA_TRUE;
}

Eina_Bool
database_class_dtor_enable_set(Eolian_Class *cl, Eina_Bool enable)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, EINA_FALSE);
   cl->class_dtor_enable = enable;
   return EINA_TRUE;
}

EAPI Eina_Bool
eolian_class_ctor_enable_get(const Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, EINA_FALSE);
   return cl->class_ctor_enable;
}

EAPI Eina_Bool
eolian_class_dtor_enable_get(const Eolian_Class *cl)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, EINA_FALSE);
   return cl->class_dtor_enable;
}

EAPI Eolian_Type_Type
eolian_type_type_get(const Eolian_Type *tp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, EOLIAN_TYPE_UNKNOWN_TYPE);
   return tp->type;
}

EAPI Eina_Iterator *
eolian_type_arguments_list_get(const Eolian_Type *tp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, NULL);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(eolian_type_type_get(tp) == EOLIAN_TYPE_FUNCTION, NULL);
   if (!tp->arguments) return NULL;
   return eina_list_iterator_new(tp->arguments);
}

EAPI Eina_Iterator *
eolian_type_subtypes_list_get(const Eolian_Type *tp)
{
   Eolian_Type_Type tpt;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, NULL);
   tpt = tp->type;
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tpt == EOLIAN_TYPE_REGULAR
                                || tpt == EOLIAN_TYPE_POINTER
                                || tpt == EOLIAN_TYPE_REGULAR_STRUCT, NULL);
   if (!tp->subtypes) return NULL;
   return eina_list_iterator_new(tp->subtypes);
}

EAPI Eina_Iterator *
eolian_type_struct_field_names_list_get(const Eolian_Type *tp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, NULL);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tp->type == EOLIAN_TYPE_STRUCT, NULL);
   return eina_hash_iterator_key_new(tp->fields);
}

EAPI Eolian_Type *
eolian_type_struct_field_get(const Eolian_Type *tp, const char *field)
{
   _Struct_Field_Type *sf = NULL;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(field, NULL);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tp->type == EOLIAN_TYPE_STRUCT, NULL);
   sf = eina_hash_find(tp->fields, field);
   if (!sf) return NULL;
   return sf->type;
}

EAPI const char *
eolian_type_struct_field_description_get(const Eolian_Type *tp, const char *field)
{
   _Struct_Field_Type *sf = NULL;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(field, NULL);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tp->type == EOLIAN_TYPE_STRUCT, NULL);
   sf = eina_hash_find(tp->fields, field);
   if (!sf) return NULL;
   return sf->comment;
}

EAPI const char *
eolian_type_struct_description_get(const Eolian_Type *tp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, NULL);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tp->type == EOLIAN_TYPE_STRUCT, NULL);
   return tp->comment;
}

EAPI Eolian_Type *
eolian_type_return_type_get(const Eolian_Type *tp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, NULL);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(eolian_type_type_get(tp) == EOLIAN_TYPE_FUNCTION, NULL);
   return tp->ret_type;
}

EAPI Eolian_Type *
eolian_type_base_type_get(const Eolian_Type *tp)
{
   Eolian_Type_Type tpt;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, NULL);
   tpt = eolian_type_type_get(tp);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tpt == EOLIAN_TYPE_POINTER, NULL);
   return tp->base_type;
}

EAPI Eina_Bool
eolian_type_is_own(const Eolian_Type *tp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, EINA_FALSE);
   return tp->is_own;
}

EAPI Eina_Bool
eolian_type_is_const(const Eolian_Type *tp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, EINA_FALSE);
   return tp->is_const;
}

static void _type_to_str(const Eolian_Type *tp, Eina_Strbuf *buf, const char *name);

static void
_ftype_to_str(const Eolian_Type *tp, Eina_Strbuf *buf, const char *name)
{
   Eina_List *l;
   Eolian_Type *stp;
   Eina_Bool first = EINA_TRUE;
   if (tp->ret_type)
     _type_to_str(tp->ret_type, buf, NULL);
   else
     eina_strbuf_append(buf, "void");
   eina_strbuf_append(buf, " (*");
   if (name) eina_strbuf_append(buf, name);
   eina_strbuf_append(buf, ")(");
   EINA_LIST_FOREACH(tp->arguments, l, stp)
     {
        if (!first) eina_strbuf_append(buf, ", ");
        first = EINA_FALSE;
        _type_to_str(stp, buf, NULL);
     }
}

static Eina_Bool
_stype_field_cb(const Eina_Hash *hash EINA_UNUSED, const void *key, void *data,
                void *fdata)
{
   _type_to_str((Eolian_Type*)((_Struct_Field_Type*)data)->type,
                (Eina_Strbuf*)fdata, (const char*)key);
   eina_strbuf_append((Eina_Strbuf*)fdata, "; ");
   return EINA_TRUE;
}

static void
_stype_to_str(const Eolian_Type *tp, Eina_Strbuf *buf, const char *name)
{
   eina_strbuf_append(buf, "struct ");
   if (tp->name)
     {
        eina_strbuf_append(buf, tp->name);
        eina_strbuf_append_char(buf, ' ');
     }
   eina_strbuf_append(buf, "{ ");
   eina_hash_foreach(tp->fields, _stype_field_cb, buf);
   eina_strbuf_append(buf, "}");
   if (name)
     {
        eina_strbuf_append_char(buf, ' ');
        eina_strbuf_append(buf, name);
     }
}

static void
_type_to_str(const Eolian_Type *tp, Eina_Strbuf *buf, const char *name)
{
   if (tp->type == EOLIAN_TYPE_FUNCTION)
     {
        _ftype_to_str(tp, buf, name);
        return;
     }
   else if (tp->type == EOLIAN_TYPE_STRUCT)
     {
        _stype_to_str(tp, buf, name);
        return;
     }
   if ((tp->type == EOLIAN_TYPE_REGULAR
     || tp->type == EOLIAN_TYPE_REGULAR_STRUCT
     || tp->type == EOLIAN_TYPE_VOID)
     && tp->is_const)
     {
        eina_strbuf_append(buf, "const ");
     }
   if (tp->type == EOLIAN_TYPE_REGULAR)
     eina_strbuf_append(buf, tp->name);
   else if (tp->type == EOLIAN_TYPE_REGULAR_STRUCT)
     {
        eina_strbuf_append(buf, "struct ");
        eina_strbuf_append(buf, tp->name);
     }
   else if (tp->type == EOLIAN_TYPE_VOID)
     eina_strbuf_append(buf, "void");
   else
     {
        Eolian_Type *btp = tp->base_type;
        _type_to_str(tp->base_type, buf, NULL);
        if (btp->type != EOLIAN_TYPE_POINTER || btp->is_const)
           eina_strbuf_append_char(buf, ' ');
        eina_strbuf_append_char(buf, '*');
        if (tp->is_const) eina_strbuf_append(buf, " const");
     }
   if (name)
     {
        eina_strbuf_append_char(buf, ' ');
        eina_strbuf_append(buf, name);
     }
}

EAPI Eina_Stringshare *
eolian_type_c_type_named_get(const Eolian_Type *tp, const char *name)
{
   Eina_Stringshare *ret;
   Eina_Strbuf *buf;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, NULL);
   buf = eina_strbuf_new();
   _type_to_str(tp, buf, name);
   ret = eina_stringshare_add(eina_strbuf_string_get(buf));
   eina_strbuf_free(buf);
   return ret;
}

EAPI Eina_Stringshare *
eolian_type_c_type_get(const Eolian_Type *tp)
{
   return eolian_type_c_type_named_get(tp, NULL);
}

EAPI Eina_Stringshare *
eolian_type_name_get(const Eolian_Type *tp)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(tp, NULL);
   eina_stringshare_ref(tp->name);
   return tp->name;
}

static Eina_Bool
_print_field(const Eina_Hash *hash EINA_UNUSED, const void *key, void *data,
             void *fdata EINA_UNUSED)
{
   printf("%s: ", (const char*)key);
   database_type_print((Eolian_Type*)data);
   puts("; ");
   return EINA_TRUE;
}

void
database_type_print(Eolian_Type *tp)
{
   Eina_List *l;
   Eolian_Type *stp;
   if (tp->is_own)
     puts("own(");
   if (tp->is_const)
     puts("const(");
   if (tp->type == EOLIAN_TYPE_REGULAR)
     puts(tp->name);
   else if (tp->type == EOLIAN_TYPE_REGULAR_STRUCT)
     printf("struct %s", tp->name);
   else if (tp->type == EOLIAN_TYPE_POINTER)
     {
        database_type_print(tp->base_type);
        putchar('*');
     }
   else if (tp->type == EOLIAN_TYPE_FUNCTION)
     {
        Eina_Bool first = EINA_TRUE;
        puts("func");
        if (tp->ret_type)
          {
             putchar(' ');
             database_type_print(tp->ret_type);
          }
        else
          puts(" void");
        puts(" (");
        EINA_LIST_FOREACH(tp->arguments, l, stp)
          {
             if (!first) puts(", ");
             first = EINA_FALSE;
             database_type_print(stp);
          }
        putchar(')');
     }
   else if (tp->type == EOLIAN_TYPE_STRUCT)
     {
        puts("struct ");
        if (tp->name) printf("%s ", tp->name);
        puts("{ ");
        eina_hash_foreach(tp->fields, _print_field, NULL);
        puts("}");
     }
   if (tp->is_own)
     putchar(')');
   if (tp->is_const)
     putchar(')');
}

static void
_implements_print(Eolian_Implement *impl, int nb_spaces)
{
   Eolian_Class *class;
   Eolian_Function *func;
   const char *t;
   Eolian_Function_Type ft;

   eolian_implement_information_get(impl, &class, &func, &ft);
   switch (ft)
     {
      case EOLIAN_PROP_SET: t = "SET"; break;
      case EOLIAN_PROP_GET: t = "GET"; break;
      case EOLIAN_METHOD: t = "METHOD"; break;
      case EOLIAN_UNRESOLVED:
        {
           t = "Type is the same as function being overriden";
           break;
        }
      default:
         return;
     }
   printf("%*s <%s> <%s>\n", nb_spaces + 5, "", eolian_implement_full_name_get(impl), t);
}

static void
_event_print(Eolian_Event *ev, int nb_spaces)
{
   const char *name, *comment, *type;

   eolian_class_event_information_get(ev, &name, &type, &comment);
   printf("%*s <%s> <%s> <%s>\n", nb_spaces + 5, "", name, type, comment);
}

static Eina_Bool _function_print(const Eolian_Function *fid, int nb_spaces)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(fid, EINA_FALSE);
   const char *ret_desc = eolian_function_return_comment_get(fid, fid->type);
   switch (fid->type)
     {
      case EOLIAN_PROPERTY:
           {
              printf("%*s<%s> %s\n", nb_spaces, "", ret_desc ? ret_desc : "", fid->name);
              const char *str = eolian_function_description_get(fid, EOLIAN_COMMENT_GET);
              if (str) printf("%*s<%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_description_get(fid, EOLIAN_COMMENT_SET);
              if (str) printf("%*s<%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_LEGACY_GET);
              if (str) printf("%*slegacy_get: <%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_LEGACY_SET);
              if (str) printf("%*slegacy_set: <%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_PROP_GET_RETURN_TYPE);
              if (str) printf("%*sreturn type for get: <%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_PROP_SET_RETURN_TYPE);
              if (str) printf("%*sreturn type for set: <%s>\n", nb_spaces + 5, "", str);
              break;
           }
      case EOLIAN_PROP_GET:
           {
              printf("%*sGET:<%s> %s\n", nb_spaces, "", ret_desc ? ret_desc : "", fid->name);
              const char *str = eolian_function_description_get(fid, EOLIAN_COMMENT_GET);
              if (str) printf("%*s<%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_LEGACY_GET);
              if (str) printf("%*slegacy: <%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_PROP_GET_RETURN_TYPE);
              if (str) printf("%*sreturn type: <%s>\n", nb_spaces + 5, "", str);
              break;
           }
      case EOLIAN_PROP_SET:
           {
              printf("%*sSET:<%s> %s\n", nb_spaces, "", ret_desc ? ret_desc : "", fid->name);
              const char *str = eolian_function_description_get(fid, EOLIAN_COMMENT_SET);
              if (str) printf("%*s<%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_LEGACY_SET);
              if (str) printf("%*slegacy: <%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_PROP_SET_RETURN_TYPE);
              if (str) printf("%*sreturn type: <%s>\n", nb_spaces + 5, "", str);
              break;
           }
      case EOLIAN_METHOD:
           {
              printf("%*s<%s> %s\n", nb_spaces, "", ret_desc ? ret_desc : "", fid->name);
              const char *str = eolian_function_description_get(fid, EOLIAN_COMMENT);
              if (str) printf("%*s<%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_LEGACY);
              if (str) printf("%*slegacy: <%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_METHOD_RETURN_TYPE);
              if (str) printf("%*sreturn type: <%s>\n", nb_spaces + 5, "", str);
              if (fid->obj_is_const) printf("%*sobj const: <true>\n", nb_spaces + 5, "");
              break;
           }
      case EOLIAN_CTOR:
           {
              //char *str = eina_hash_find(fid->data, "comment");
              const char *str = eolian_function_description_get(fid, EOLIAN_COMMENT);
              if (str) printf("%*s<%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_LEGACY);
              if (str) printf("%*slegacy: <%s>\n", nb_spaces + 5, "", str);
              str = eolian_function_data_get(fid, EOLIAN_METHOD_RETURN_TYPE);
              if (str) printf("%*sreturn type: <%s>\n", nb_spaces + 5, "", str);
              break;
           }
      default:
         return EINA_FALSE;
     }
   Eina_List *itr;
   Eolian_Function_Parameter *param;
   EINA_LIST_FOREACH(fid->params, itr, param)
     {
        char *param_dir = NULL;
        switch (param->param_dir)
          {
           case EOLIAN_IN_PARAM:
             param_dir = "IN";
             break;
           case EOLIAN_OUT_PARAM:
             param_dir = "OUT";
             break;
           case EOLIAN_INOUT_PARAM:
             param_dir = "INOUT";
             break;
          }
         printf("%*s%s <%s> <", nb_spaces + 5, "", param_dir, param->name);
         database_type_print((Eolian_Type*)param->type);
         printf("> <%s>\n", param->description?param->description:"");
     }
   return EINA_TRUE;
}

static Eina_Bool
_class_print(const Eolian_Class *cl)
{
   Eina_List *itr;
   Eolian_Function *function;
   const char *types[5] = {"", "Regular", "Regular Non Instantiable", "Mixin", "Interface"};

   EINA_SAFETY_ON_NULL_RETURN_VAL(cl, EINA_FALSE);
   printf("Class %s:\n", cl->name);
   if (cl->description)
     printf("  description: <%s>\n", cl->description);

   printf("  type: %s\n", types[cl->type]);

   // Inherits
   if (cl->inherits)
     {
        printf("  inherits: ");
        char *word;
        EINA_LIST_FOREACH(cl->inherits, itr, word)
          printf("%s ", word);
        printf("\n");
     }

   // Legacy prefix
   if (cl->legacy_prefix)
     {
        printf("  legacy prefix: <%s>\n", cl->legacy_prefix);
     }

   // Eo prefix
   if (cl->eo_prefix)
     {
        printf("  Eo prefix: <%s>\n", cl->eo_prefix);
     }

   // Data type
   if (cl->data_type)
     {
        printf("  Data type: <%s>\n", cl->data_type);
     }

   // Constructors
   printf("  constructors:\n");
   EINA_LIST_FOREACH(cl->constructors, itr, function)
     {
        _function_print(function, 4);
     }
   printf("\n");

   // Properties
   printf("  properties:\n");
   EINA_LIST_FOREACH(cl->properties, itr, function)
     {
        _function_print(function, 4);
     }
   printf("\n");

   // Methods
   printf("  methods:\n");
   EINA_LIST_FOREACH(cl->methods, itr, function)
     {
        _function_print(function, 4);
     }
   // Implement
   printf("  implements:\n");
   Eolian_Implement *impl;
   EINA_LIST_FOREACH(cl->implements, itr, impl)
     {
        _implements_print(impl, 4);
     }
   printf("\n");
   // Implement
   printf("  events:\n");
   Eolian_Event *ev;
   EINA_LIST_FOREACH(cl->events, itr, ev)
     {
        _event_print(ev, 4);
     }
   printf("\n");
   return EINA_TRUE;
}

EAPI Eina_Bool
eolian_show(const Eolian_Class *class)
{
   if (!class)
     {
        Eina_List *itr;
        Eolian_Class *cl;
        EINA_LIST_FOREACH(_classes, itr, cl)
          _class_print(cl);
     }
   else
     {
        _class_print(class);
     }
   return EINA_TRUE;
}

#define EO_SUFFIX ".eo"
#define EOT_SUFFIX ".eot"

static char *
join_path(const char *path, const char *file)
{
   Eina_Strbuf *buf = eina_strbuf_new();
   char *ret;

   eina_strbuf_append(buf, path);
   eina_strbuf_append_char(buf, '/');
   eina_strbuf_append(buf, file);

   ret = eina_strbuf_string_steal(buf);
   eina_strbuf_free(buf);
   return ret;
}

static void
_scan_cb(const char *name, const char *path, void *data EINA_UNUSED)
{
   size_t len;
   Eina_Bool is_eo = eina_str_has_suffix(name, EO_SUFFIX);
   if (!is_eo && !eina_str_has_suffix(name, EOT_SUFFIX)) return;
   len = strlen(name) - (is_eo ? sizeof(EO_SUFFIX) : sizeof(EOT_SUFFIX)) + 1;
   eina_hash_add(is_eo ? _filenames : _tfilenames,
                 eina_stringshare_add_length(name, len), join_path(path, name));
}

EAPI Eina_Bool
eolian_directory_scan(const char *dir)
{
   if (!dir) return EINA_FALSE;
   eina_file_dir_list(dir, EINA_TRUE, _scan_cb, NULL);
   return EINA_TRUE;
}

EAPI Eina_Bool
eolian_system_directory_scan()
{
   Eina_Bool ret;
   Eina_Strbuf *buf = eina_strbuf_new();
   eina_strbuf_append(buf, eina_prefix_data_get(_eolian_prefix));
   eina_strbuf_append(buf, "/include");
   ret = eolian_directory_scan(eina_strbuf_string_get(buf));
   eina_strbuf_free(buf);
   return ret;
}

static char *
_eolian_class_to_filename(const char *filename)
{
   char *ret;
   Eina_Strbuf *strbuf = eina_strbuf_new();
   eina_strbuf_append(strbuf, filename);
   eina_strbuf_replace_all(strbuf, ".", "_");

   ret = eina_strbuf_string_steal(strbuf);
   eina_strbuf_free(strbuf);

   eina_str_tolower(&ret);

   return ret;
}

EAPI Eina_Bool
eolian_eot_file_parse(const char *filepath)
{
   return eo_parser_database_fill(filepath, EINA_TRUE);
}

EAPI Eina_Bool
eolian_eo_file_parse(const char *filepath)
{
   const Eina_List *itr;
   Eolian_Class *class = eolian_class_find_by_file(filepath);
   const char *inherit_name;
   Eolian_Implement *impl;
   if (!class)
     {
        if (!eo_parser_database_fill(filepath, EINA_FALSE)) return EINA_FALSE;
        class = eolian_class_find_by_file(filepath);
        if (!class)
          {
             ERR("No class for file %s", filepath);
             return EINA_FALSE;
          }
     }
   EINA_LIST_FOREACH(eolian_class_inherits_list_get(class), itr, inherit_name)
     {
        if (!eolian_class_find_by_name(inherit_name))
          {
             char *filename = _eolian_class_to_filename(inherit_name);
             filepath = eina_hash_find(_filenames, filename);
             if (!filepath)
               {
                  ERR("Unable to find a file for class %s", inherit_name);
                  return EINA_FALSE;
               }
             if (!eolian_eo_file_parse(filepath)) return EINA_FALSE;
             if (!eolian_class_find_by_name(inherit_name))
               {
                  ERR("Unable to find class %s", inherit_name);
                  return EINA_FALSE;
               }
             free(filename);
          }
     }
   EINA_LIST_FOREACH(eolian_class_implements_list_get(class), itr, impl)
     {
        Eolian_Class *impl_class;
        Eolian_Function *impl_func;
        Eolian_Function_Type impl_type = EOLIAN_UNRESOLVED;
        eolian_implement_information_get(impl, &impl_class, &impl_func, &impl_type);
        if (!impl_func)
          {
             ERR("Unable to find function %s", eolian_implement_full_name_get(impl));
             return EINA_FALSE;
          }
     }
   return EINA_TRUE;
}

static Eina_Bool _tfile_parse(const Eina_Hash *hash EINA_UNUSED, const void *key EINA_UNUSED, void *data, void *fdata)
{
   Eina_Bool *ret = fdata;
   if (*ret) *ret = eolian_eot_file_parse(data);
   return *ret;
}

EAPI Eina_Bool
eolian_all_eot_files_parse()
{
   Eina_Bool ret = EINA_TRUE;
   eina_hash_foreach(_tfilenames, _tfile_parse, &ret);
   return ret;
}

static Eina_Bool _file_parse(const Eina_Hash *hash EINA_UNUSED, const void *key EINA_UNUSED, void *data, void *fdata)
{
   Eina_Bool *ret = fdata;
   if (*ret) *ret = eolian_eo_file_parse(data);
   return *ret;
}

EAPI Eina_Bool
eolian_all_eo_files_parse()
{
   Eina_Bool ret = EINA_TRUE;
   eina_hash_foreach(_filenames, _file_parse, &ret);
   return ret;
}

