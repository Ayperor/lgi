/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

#define G_LOG_DOMAIN "Lgi"

#include <lua.h>
#include <lauxlib.h>

/* Lua 5.2 compatibility stuff. */
#if LUA_VERSION_NUM >= 502
#define luaL_register(L, null, regs) luaL_setfuncs (L, regs, 0)
#define lua_equal(L, p1, p2) lua_compare (L, p1, p2, LUA_OPEQ)
#define lua_objlen(L, p) lua_rawlen (L, p)
#define lua_setfenv(L, p) lua_setuservalue (L, p)
#define lua_getfenv(L, p) lua_getuservalue (L, p)
#endif

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>
#include <girepository.h>

/* Makes sure that Lua stack offset is absolute one, not relative. */
#define lgi_makeabs(L, x) do { if (x < 0) x += lua_gettop (L) + 1; } while (0)

/* Puts parts of the name to the stack, to be concatenated by lua_concat.
   Returns number of pushed elements. */
int lgi_type_get_name (lua_State *L, GIBaseInfo *info);

/* Stores repo type table associated with specified gtype (or BaseInfo
   if gtype is invalid). to the stack, or nil if no such table can be
   found in the repo. */
void lgi_type_get_repotype (lua_State *L, GType gtype, GIBaseInfo *info);

/* Gets GType from Lua index narg.  Accepts number and when it is
   other type, invokes Lua helper to convert. */
GType lgi_type_get_gtype (lua_State *L, int narg);

/* Allocates guard, a pointer-size userdata with associated destroy
   handler. Returns pointer to user_data stored inside guard. */
gpointer *lgi_guard_create (lua_State *L, GDestroyNotify destroy);

/* lightuserdata of this address is a key in LUA_REGISTRYINDEX table
   to global repo table. */
extern int lgi_addr_repo;

/* Creates cache table (optionally with given table __mode), stores it
   into registry to specified userdata address. */
void
lgi_cache_create (lua_State *L, gpointer key, const char *mode);

/* Initialization of modules. */
void lgi_marshal_init (lua_State *L);
void lgi_record_init (lua_State *L);
void lgi_object_init (lua_State *L);
void lgi_callable_init (lua_State *L);
void lgi_gi_init (lua_State *L);
void lgi_buffer_init (lua_State *L);

/* Checks whether given argument is of specified udata - similar to
   luaL_testudata, which is missing in Lua 5.1 */
void *
lgi_udata_test (lua_State *L, int narg, const char *name);

/* Metatable name of userdata for 'bytes' extension; see
   http://permalink.gmane.org/gmane.comp.lang.lua.general/79288 */
#define LGI_BYTES_BUFFER "bytes.bytearray"

/* Metatable name of userdata - gi wrapped 'GIBaseInfo*' */
#define LGI_GI_INFO "lgi.gi.info"

/* Creates new instance of info from given GIBaseInfo pointer. */
int lgi_gi_info_new (lua_State *L, GIBaseInfo *info);

/* Assumes that 'typetable' can hold field 'name' which contains
   wrapped LGI_GI_INFO of function.  Returns address of this function,
   NULL if table does not contain such field. */
gpointer lgi_gi_load_function(lua_State *L, int typetable, const char *name);

/* Retrieve synchronization state, which can be used for entering and
   leaving the state using lgi_state_enter() and lgi_state_leave(). */
gpointer lgi_state_get_lock (lua_State *L);

/* Enters/leaves Lua state. */
void lgi_state_enter (gpointer left_state);
void lgi_state_leave (gpointer state_lock);

/* Marshalls single value from Lua to GLib/C. Returns number of temporary
   entries pushed to Lua stack, which should be popped before function call
   returns. */
int lgi_marshal_2c (lua_State *L, GITypeInfo *ti, GIArgInfo *ai,
		    GITransfer xfer,  gpointer target, int narg,
		    int parent, GICallableInfo *ci, void **args);

/* If given parameter is out:caller-allocates, tries to perform
   special 2c marshalling.  If not needed, returns FALSE, otherwise
   stores single value with value prepared to be returned to C. */
gboolean lgi_marshal_2c_caller_alloc (lua_State *L, GITypeInfo *ti,
				      GIArgument *target, int pos);

/* Marshalls single value from GLib/C to Lua. If parent is non-0, it
   is stack index of parent structure/array in which this C value
   resides. */
void lgi_marshal_2lua (lua_State *L, GITypeInfo *ti, GITransfer xfer,
		       gpointer source, int parent,
		       GICallableInfo *ci, void **args);

/* Marshalls field to/from given memory (struct, union or
   object). Returns number of results pushed to the stack (0 or 1). */
int lgi_marshal_field (lua_State *L, gpointer object, gboolean getmode,
		       int parent_arg, int field_arg, int val_arg);

/* Implementation of object/record _access invocation. */
int lgi_marshal_access (lua_State *L, gboolean getmode,
			int compound_arg, int element_arg, int val_arg);

/* Parses given GICallableInfo, creates new userdata for it and stores
   it to the stack. Uses cache, so already parsed callable held in the
   cache is reused if possible. */
int lgi_callable_create (lua_State *L, GICallableInfo *ci, gpointer addr);

/* Creates container block for allocated closures.  Returns address of
   the block, suitable as user_data parameter. */
gpointer lgi_closure_allocate (lua_State *L, int count);

/* Allocates n-th closure in the closure block for specified Lua
   function (or callable table or userdata). Returns executable
   address for the closure. */
gpointer lgi_closure_create (lua_State* L, gpointer user_data,
			     GICallableInfo* ci, int target,
			     gboolean autodestroy);

/* GDestroyNotify-compatible callback for destroying closure. */
void lgi_closure_destroy (gpointer user_data);

/* Allocates and creates new record instance. */
gpointer lgi_record_new (lua_State *L, GIBaseInfo *ri);

/* Creates Lua-side part of given record. Assumes that repotype table
   is on the stack, replaces it with newly created proxy. If parent
   not zero, it is stack index of record parent (i.e. record of which
   the arg record is part of). */
void lgi_record_2lua (lua_State *L, gpointer addr, gboolean own, int parent);

/* Gets pointer to C-structure from given Lua-side object. Expects
   repo typetable of expected argument pushed on the top of the stack,
   removes it. */
gpointer lgi_record_2c (lua_State *L, int narg, gboolean optional,
			gboolean nothrow);

/* Creates Lua-side part (proxy) of given object. If the object is not
   owned (own == FALSE), an ownership is automatically acquired.  Returns
   number of elements pushed to the stack, i.e. always 1. */
int
lgi_object_2lua (lua_State *L, gpointer obj, gboolean own);

/* Gets pointer to C-side object represented by given Lua proxy. If
   gtype is not G_TYPE_INVALID, the real type is checked to conform to
   requested type. */
gpointer
lgi_object_2c (lua_State *L, int narg, GType gtype, gboolean optional,
	       gboolean nothrow);

#if !GLIB_CHECK_VERSION(2, 30, 0)
/* Workaround for broken g_struct_info_get_size() for GValue, see
   https://bugzilla.gnome.org/show_bug.cgi?id=657040 */
gsize lgi_struct_info_get_size (GIStructInfo *info);
#define g_struct_info_get_size lgi_struct_info_get_size
int lgi_field_info_get_offset (GIFieldInfo *info);
#define g_field_info_get_offset lgi_field_info_get_offset
#endif

/* Workaround method for broken g_object_info_get_*_function_pointer()
   in GI 1.32.0. (see https://bugzilla.gnome.org/show_bug.cgi?id=673282) */
gpointer lgi_object_get_function_ptr (GIObjectInfo *info,
				      const gchar *(*getter)(GIObjectInfo *));
