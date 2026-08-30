# Porting cpp-mcp to the Crust C++ subset

This fork is being adapted to compile under
[Crust](https://github.com/brentharts/crust), whose `tools/cpprust.py`
lowers a subset of C++ to C. The subset is documented in Crust's
`CPPRUST.md`; what follows is only what it means *here* — which idioms
changed, and why each one had to.

**Every change keeps the ordinary build working.** `./build.py --examples`
compiles the library and all five examples with `g++` after each step, and
that is the check that catches mistakes. Nothing below is conditional on a
toolchain except the one case that says so.

## Checking the port

```bash
./build.py --examples                       # must stay clean
python3 /path/to/crust/tools/cpprust.py src/mcp_message.cpp \
    -o /tmp/out.c --incdir include --incdir <stubs> -D MCP_CRUST
```

`-D MCP_CRUST` selects the subset-only spellings (see *Exceptions* below).
The `<stubs>` directory stands in for `common/json.hpp`,
`common/httplib.h` and `common/base64.hpp`, none of which the subset can
translate yet — see *Not yet started*.

| file | translates |
|---|---|
| `src/mcp_message.cpp` | yes |
| `src/mcp_tool.cpp` | yes |
| `src/mcp_resource.cpp` | blocked on `throw` |
| `src/mcp_server.cpp` | blocked on the handler table |
| `src/mcp_sse_client.cpp` | blocked on the handler table |
| `src/mcp_stdio_client.cpp` | blocked on iterating a `json` |

## What changed, and why

### String concatenation is built with `+=`

`"Resource not found: " + uri` passed straight into a function is out. An
overloaded operator's result is a value that lives nowhere, and a reference
parameter is lowered to a pointer, so there is no address to pass. Every
such site now builds a named local first:

```cpp
std::string msg("Resource not found: ");
msg += uri;
throw mcp_exception(error_code::invalid_params, msg);
```

Chains (`a + b + c`) go the same way, one `+=` per part.

`file_resource`'s constructor could not do this — its URI was built in the
initializer list, where there is no statement to hoist a local into. Two
changes together fix it: a `make_file_uri` helper that returns the string,
and `text_resource` taking its `uri` parameter **by value**. A by-value
parameter is constructed at the call, so it accepts a computed value; a
reference parameter cannot.

The same reasoning is why `tool_builder::add_param` takes `type` by value
and `with_array_param` takes `item_type` by value: both are called with a
string literal, and a literal has no address either.

### `auto` where the type is not written

`auto it = resources_.find(uri)` has nothing to deduce from — an iterator's
type is not written anywhere. Those idioms are now `count` and
`erase(key)`. The subscription cleanup collects keys into a vector first and
erases afterwards, which also fixes a latent bug: the original erased while
walking, invalidating its own iterator.

Elsewhere `auto` was replaced with the written type (`request`,
`const json&`, `std::shared_ptr<event_dispatcher>`, `std::future_status`,
`resource_template_entry`). Structured bindings over maps are fine and were
left alone.

**`auto` is not a translation-speed problem.** Measured: 300 `auto`
declarations versus 300 written types translate in 0.53s and 0.52s — noise.
What `auto` causes is outright *failure* where deduction has no source to
read, and that is the only reason any of it was touched.

### Storing a callable

`std::function` cannot be stored. It holds a callable, and the callable one
naturally writes is a capturing lambda — which the subset inlines at its
call sites and so has no value to put in a container. Taken *by reference*
it is a borrow and is fine; it is storing that is out.

The subscription map is now a plain function pointer plus a context
pointer:

```cpp
typedef void (*resource_changed_fn)(void* context, const std::string& uri);

struct subscription {
    std::string uri;
    resource_changed_fn fn;
    void* context;
};
```

The context carries what a capture would have carried, which is what every
C callback API does.

### Builder chains return pointers

A reference return is not in the subset, so `tool_builder&` became
`tool_builder*`. Call sites chain with `->` after the first hop:

```cpp
mcp::tool t = mcp::tool_builder("calculator")
    .with_description("Perform basic calculations")
    ->with_number_param("a", "First operand")
    ->build();
```

The first hop stays `.` because it is on an object. `prompt_builder` still
returns references and is unchanged.

### Exceptions — the one conditional

`mcp_exception` derived from `std::runtime_error`, which the subset has no
`<stdexcept>` for; a base also has to be a class defined above the one
deriving from it. Removing the base outright would have been wrong: a user
tool handler can throw `mcp_exception`, and the handler around it catches
`const std::exception&`, so those would have escaped to `terminate()` —
silently, and only on the error path.

So the base is selected by the toolchain:

```cpp
#ifdef MCP_CRUST
class mcp_error_base { /* msg_, what() */ };
#else
typedef std::runtime_error mcp_error_base;
#endif
class mcp_exception : public mcp_error_base { ... };
```

`g++` keeps identical catch semantics; the subset gets a base it can lay
out.

### Fallible work leaves constructors

`file_resource`'s constructor checked that the file existed and threw. A
constructor has no return value to report a failure through, and a throw
from one leaves a partially-built object. The check now lives in
`file_resource::create`, which does the fallible work first and builds the
object only once it has succeeded.

A `catch (...)` around subscriber callbacks was removed rather than ported.
Swallowing a callback's error silently was never much of a policy.

## Not yet started

**`throw` and `catch` — 37 and 41 sites.** The subset refuses both
permanently; its replacement is a checked model spelled
`try { .. } except (long e) { .. }` with `raise`. **That syntax is not
valid C++**, so adopting it ends the dual build this port has relied on
throughout. Three ways out, none obviously right:

1. Macro-bridge `throw`/`try`/`catch` behind `MCP_CRUST`, as the exception
   base already is. Works, but control-flow keywords are far uglier to
   macro than a class.
2. Fork the error handling and maintain two sources.
3. Convert the API to return codes. Largest change, touches the public
   interface, but suits an RPC library and sidesteps the question.

This needs a decision before anyone starts.

**The handler table.** `mcp_server` and `mcp_sse_client` dispatch through
`std::map<std::string, std::function<...>>` filled with `[this]` lambdas —
the same storing-a-callable wall the subscriptions hit, at much larger
scale (24 sites). The subscription rewrite above is the pattern; the thread
pool's task queue needs it too.

**The vendored headers.** `common/json.hpp` (24,766 lines) is reached by
`mcp_message.h`, so nothing in the library translates until it is dealt
with; it is built on exceptions, SFINAE and stream operators, four things
the subset either refuses or does not have. `common/httplib.h` (10,440
lines) dominates compile time and binary size. `common/base64.hpp` is
iterator-template based. Replacing json with a Crust-side implementation
looks better than porting it: a JSON value is a tagged union owning a
buffer, which is exactly the shape a Crust type with `impl Drop` already
has.

**Iterating a `json`.** `mcp_stdio_client` walks a `json` with a
range-`for`. The subset walks a container with `size()` and `operator[]`;
iterators are a separate feature.
